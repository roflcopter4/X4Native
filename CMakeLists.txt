cmake_minimum_required(VERSION 3.20)
project(x4native VERSION 0.9.0)

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
# X4_GAME_DIR resolution order:
#   1. Explicit -DX4_GAME_DIR=<path> or CMakeUserPresets.json
#   2. Auto-detect from Steam registry + verify X4 is installed there
set(X4_GAME_DIR "" CACHE PATH "X4 Foundations installation directory")

if(NOT X4_GAME_DIR AND WIN32)
    # Read Steam's install path from the registry
    get_filename_component(_STEAM_DIR
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam;InstallPath]"
        ABSOLUTE CACHE)

    if(_STEAM_DIR AND EXISTS "${_STEAM_DIR}")
        set(_X4_CANDIDATE "${_STEAM_DIR}/steamapps/common/X4 Foundations")
        if(EXISTS "${_X4_CANDIDATE}/X4.exe")
            set(X4_GAME_DIR "${_X4_CANDIDATE}" CACHE PATH
                "X4 Foundations installation directory (auto-detected)" FORCE)
            message(STATUS "Auto-detected X4 at: ${X4_GAME_DIR}")
        endif()
    endif()
    unset(_STEAM_DIR CACHE)
    unset(_X4_CANDIDATE)
endif()

if(NOT X4_GAME_DIR)
    message(WARNING "X4_GAME_DIR not set and auto-detect failed.\n"
                    "Deploy target will be unavailable.\n"
                    "Pass -DX4_GAME_DIR=<path> or create CMakeUserPresets.json")
endif()

set(DEPLOY_DIR "${X4_GAME_DIR}/extensions/x4native")

# ---------------------------------------------------------------------------
# DLL build (Phase 1+, when src/ exists)
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/src/CMakeLists.txt")
    enable_language(CXX)
    add_subdirectory(src)
else()
    message(STATUS "src/ not found — skipping DLL build (extension-files-only deploy)")
endif()

# ---------------------------------------------------------------------------
# Install rules — these define WHAT gets deployed
# ---------------------------------------------------------------------------
# Extension manifest
install(FILES extension/content.xml extension/ui.xml DESTINATION .)

# Lua bootstrap
install(FILES extension/ui/x4native.lua DESTINATION ui)

# MD scripts
install(FILES extension/md/x4native_main.xml DESTINATION md)

# Translations
install(FILES extension/t/0001-L044.xml DESTINATION t)

# Framework settings — only deploy if not already present (preserves user edits)
install(CODE "
    set(_SETTINGS_DEST \"\${CMAKE_INSTALL_PREFIX}/x4native_settings.json\")
    if(NOT EXISTS \"\${_SETTINGS_DEST}\")
        file(INSTALL \"${CMAKE_SOURCE_DIR}/extension/x4native_settings.json\"
             DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")
    else()
        message(STATUS \"Skipping x4native_settings.json (already exists)\")
    endif()
")

# DLL targets are installed by src/CMakeLists.txt.
# version_db/ — internal function RVA database (needed at runtime for hook resolution)
install(DIRECTORY native/version_db/ DESTINATION native/version_db)

# SDK headers (for extension developers — opt-in, not included in player/Workshop releases)
option(INSTALL_SDK "Install SDK headers alongside the extension" ON)
if(INSTALL_SDK)
    install(DIRECTORY sdk/ DESTINATION sdk)
endif()

# ---------------------------------------------------------------------------
# deploy target — single command: cmake --build build --target deploy
# ---------------------------------------------------------------------------
if(X4_GAME_DIR)
    add_custom_target(deploy
        COMMAND ${CMAKE_COMMAND} -E echo "Deploying x4native to ${DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR}
                --prefix "${DEPLOY_DIR}"
                --config $<CONFIG>
        COMMENT "Deploy x4native extension"
    )

    # If DLL targets exist, make deploy depend on them so they build first
    if(TARGET x4native_64)
        add_dependencies(deploy x4native_64)
    endif()
    if(TARGET x4native_core)
        add_dependencies(deploy x4native_core)
    endif()
else()
    add_custom_target(deploy
        COMMAND ${CMAKE_COMMAND} -E echo "ERROR: X4_GAME_DIR is not set. Cannot deploy."
        COMMAND ${CMAKE_COMMAND} -E false
    )
endif()
