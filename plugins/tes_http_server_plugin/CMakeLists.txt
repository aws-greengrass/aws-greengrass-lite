cmake_minimum_required(VERSION 3.22)
project(tes_http_server_plugin)

include(../../common.cmake)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

add_subdirectory(../device-sdk/ device-sdk EXCLUDE_FROM_ALL)

add_library(tes_http_server_plugin MODULE src/main.cpp src/tes_http_server.cpp
                                          src/tes_http_server.hpp)

target_link_libraries(tes_http_server_plugin PRIVATE gg_plugin_api device-sdk)

configureRPATH(tes_http_server_plugin)

if(LINUX)
  target_link_options(
    tes_http_server_plugin PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

install(TARGETS tes_http_server_plugin DESTINATION plugins)
