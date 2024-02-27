cmake_minimum_required(VERSION 3.22)
project(cli_server)

include(../../common.cmake)

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

add_library(cli_server MODULE src/cli_server.cpp src/cli_server.hpp
                              src/main.cpp)

target_link_libraries(cli_server PRIVATE gg_plugin_api)

if(LINUX)
  target_link_options(
    cli_server PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

install(TARGETS cli_server DESTINATION plugins)
