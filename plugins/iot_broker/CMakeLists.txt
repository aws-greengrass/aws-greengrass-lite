cmake_minimum_required(VERSION 3.25)
project(iot_broker)

set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)

FetchContent_Declare(
  device-sdk-cpp
  GIT_REPOSITORY https://github.com/aws/aws-iot-device-sdk-cpp-v2.git
  GIT_TAG v1.29.0)

FetchContent_GetProperties(device-sdk-cpp)
if (NOT device-sdk-cpp_POPULATED)
    FetchContent_Populate(device-sdk-cpp)
    add_subdirectory(${device-sdk-cpp_SOURCE_DIR} ${device-sdk-cpp_BINARY_DIR} EXCLUDE_FROM_ALL)
endif ()

add_library(iot_broker MODULE main.cpp)
target_link_libraries(iot_broker PRIVATE nucleus-core aws-crt-cpp)

install(TARGETS iot_broker DESTINATION plugins)
