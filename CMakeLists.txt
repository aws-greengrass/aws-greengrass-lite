cmake_minimum_required(VERSION 3.22)
project(greengrass-lite)

include(./common.cmake)

# Dependencies
find_package(Threads REQUIRED)

fetchContentAddCmake(Catch2 trompeloeil)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
target_compile_features(Catch2 PRIVATE cxx_std_17)

# Targets
add_subdirectory(plugin_api)
add_subdirectory(nucleus)
add_subdirectory(plugins)

add_test(NAME nucleus COMMAND nucleus-tests)
