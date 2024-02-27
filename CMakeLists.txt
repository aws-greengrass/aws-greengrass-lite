cmake_minimum_required(VERSION 3.22)
project(greengrass-lite)

include(./common.cmake)

# Targets
add_subdirectory(plugin_api)
add_subdirectory(nucleus)
add_subdirectory(plugins)
