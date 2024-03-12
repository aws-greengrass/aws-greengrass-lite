cmake_minimum_required(VERSION 3.25)
project(iot_broker)

include(../../common.cmake)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)
add_subdirectory(../shared_resources shared_resources EXCLUDE_FROM_ALL)

add_subdirectory(../device-sdk/ device-sdk EXCLUDE_FROM_ALL)

enable_warnings()

add_library(iot_broker MODULE src/iot_broker.cpp src/iot_broker.hpp
                              src/main.cpp src/tes.cpp)

target_link_libraries(iot_broker PRIVATE gg_plugin_api device-sdk
                                         shared_resources)

configureRPATH(iot_broker)

set_version_script(iot_broker "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(TARGETS iot_broker DESTINATION plugins)
