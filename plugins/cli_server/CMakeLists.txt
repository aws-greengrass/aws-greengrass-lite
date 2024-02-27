cmake_minimum_required(VERSION 3.22)
project(cli_server)

add_library(cli_server MODULE src/cli_server.cpp src/cli_server.hpp
                              src/main.cpp)
target_link_libraries(cli_server PRIVATE gg_plugin_api)
target_include_directories(cli_server PRIVATE src)
if(LINUX)
  target_link_options(
    cli_server PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

install(TARGETS cli_server DESTINATION plugins)
