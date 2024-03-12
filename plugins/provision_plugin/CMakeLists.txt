cmake_minimum_required(VERSION 3.25)
project(provision_plugin)

include(../../common.cmake)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

add_subdirectory(../device-sdk/ device-sdk EXCLUDE_FROM_ALL)

enable_warnings()

add_library(provision_plugin MODULE src/main.cpp src/provision_plugin.cpp
                                    src/provision_plugin.hpp)

target_link_libraries(provision_plugin PRIVATE gg_plugin_api device-sdk)

configureRPATH(provision_plugin)

set_version_script(provision_plugin
                   "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(TARGETS provision_plugin DESTINATION plugins)
