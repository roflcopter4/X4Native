# ---------------------------------------------------------------------------
# src/CMakeLists.txt — Build rules for proxy + core DLLs
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output DLLs directly into native/ (all configs) so the symlink dev-mode
# and the deploy target both find them in the same place.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/native")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/native")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/native")

# ---------------------------------------------------------------------------
# Dependencies (fetched at configure time)
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.3
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(json)

# MinHook v1.3.3 has no CMakeLists.txt — fetch sources only, then build manually.
FetchContent_GetProperties(minhook)
if(NOT minhook_POPULATED)
    FetchContent_Populate(minhook)
endif()

add_library(minhook STATIC
    ${minhook_SOURCE_DIR}/src/buffer.c
    ${minhook_SOURCE_DIR}/src/hook.c
    ${minhook_SOURCE_DIR}/src/trampoline.c
    ${minhook_SOURCE_DIR}/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# Proxy DLL — thin, stable, ~100 lines
# ---------------------------------------------------------------------------
add_library(x4native_64 SHARED
    proxy/proxy.cpp
    common/lua_api.cpp
)

target_include_directories(x4native_64 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)

# Debug-only: proxy needs nlohmann_json for settings file parsing (autoreload)
target_link_libraries(x4native_64 PRIVATE
    $<$<CONFIG:Debug>:nlohmann_json::nlohmann_json>
)

# No lib prefix (produce x4native_64.dll, not libx4native_64.dll)
set_target_properties(x4native_64 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "x4native_64"
)

# ---------------------------------------------------------------------------
# Core DLL — hot-reloadable, contains all framework logic
# ---------------------------------------------------------------------------
add_library(x4native_core SHARED
    core/core.cpp
    core/logger.cpp
    core/event_system.cpp
    core/extension_manager.cpp
    core/game_api.cpp
    core/hook_manager.cpp
    core/version.cpp
)

target_include_directories(x4native_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/sdk
)

target_link_libraries(x4native_core PRIVATE
    nlohmann_json::nlohmann_json
    minhook
    version       # Windows Version API (GetFileVersionInfo)
)

set_target_properties(x4native_core PROPERTIES
    PREFIX ""
    OUTPUT_NAME "x4native_core"
)

# ---------------------------------------------------------------------------
# Install rules — targets go into native/ under the install prefix
# ---------------------------------------------------------------------------
install(TARGETS x4native_64 x4native_core
    RUNTIME DESTINATION native
)
