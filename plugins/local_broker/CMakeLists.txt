cmake_minimum_required(VERSION 3.25)
project(local_broker)

include(../../common.cmake)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)
add_subdirectory(../shared_resources shared_resources EXCLUDE_FROM_ALL)

enable_warnings()

add_library(local_broker MODULE src/main.cpp)

target_link_libraries(local_broker PRIVATE gg_plugin_api shared_resources)

set_version_script(local_broker "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(TARGETS local_broker DESTINATION plugins)
