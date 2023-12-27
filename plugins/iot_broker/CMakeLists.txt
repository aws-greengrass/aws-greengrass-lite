cmake_minimum_required(VERSION 3.22)
project(iot_broker)

include(FetchContent)
include(../../utils/fetchContentFromDeps.cmake)

fetchcontentfromdeps(device-sdk-cpp)

FetchContent_GetProperties(device-sdk-cpp)
if(NOT device-sdk-cpp_POPULATED)
  FetchContent_Populate(device-sdk-cpp)
  add_subdirectory(${device-sdk-cpp_SOURCE_DIR} ${device-sdk-cpp_BINARY_DIR}
                   EXCLUDE_FROM_ALL)
endif()

add_library(iot_broker MODULE main.cpp src/iot_broker.cpp src/iot_broker.hpp)
target_link_libraries(iot_broker PRIVATE nucleus-core aws-crt-cpp
                                         shared_resources)
if(UNIX AND NOT APPLE)
  target_link_options(
    iot_broker PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

install(TARGETS iot_broker DESTINATION plugins)
