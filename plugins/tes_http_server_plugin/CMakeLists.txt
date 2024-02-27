cmake_minimum_required(VERSION 3.22)
project(tes_http_server_plugin)

include(../../common.cmake)

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

fetchContentAddCmake(device-sdk-cpp)

add_library(tes_http_server_plugin MODULE src/main.cpp src/tes_http_server.cpp
                                          src/tes_http_server.hpp)
target_link_libraries(tes_http_server_plugin PRIVATE gg_plugin_api aws-c-http)
target_include_directories(
  tes_http_server_plugin
  PUBLIC include
  PRIVATE src)

if(LINUX)
  target_link_options(
    tes_http_server_plugin PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

install(TARGETS tes_http_server_plugin DESTINATION plugins)
