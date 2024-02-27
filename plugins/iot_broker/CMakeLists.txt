cmake_minimum_required(VERSION 3.22)
project(iot_broker)

include(../../utils/fetchContentFromDeps.cmake)

fetchContentAddCmake(device-sdk-cpp)

add_library(iot_broker MODULE src/iot_broker.cpp src/iot_broker.hpp
                              src/main.cpp src/tes.cpp)
target_link_libraries(iot_broker PRIVATE gg_plugin_api aws-crt-cpp
                                         shared_resources)
target_include_directories(iot_broker PRIVATE src)
if(UNIX AND NOT APPLE)
  target_link_options(
    iot_broker PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

install(TARGETS iot_broker DESTINATION plugins)
