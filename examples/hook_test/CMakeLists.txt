cmake_minimum_required(VERSION 3.20)
project(x4native_hook_test)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(x4native_hook_test SHARED hook_test.cpp)

target_include_directories(x4native_hook_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
)

set_target_properties(x4native_hook_test PROPERTIES
    PREFIX ""
    OUTPUT_NAME "x4native_hook_test"
)
