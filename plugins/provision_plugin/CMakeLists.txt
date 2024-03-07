cmake_minimum_required(VERSION 3.22)
project(provision_plugin)

include(../../common.cmake)

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

add_subdirectory(../device-sdk/ device-sdk EXCLUDE_FROM_ALL)

add_library(provision_plugin MODULE src/main.cpp src/provision_plugin.cpp
                                    src/provision_plugin.hpp)

target_link_libraries(provision_plugin PRIVATE gg_plugin_api device-sdk)

configureRPATH(provision_plugin)

if(LINUX)
  target_link_options(
    provision_plugin PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

install(TARGETS provision_plugin DESTINATION plugins)
