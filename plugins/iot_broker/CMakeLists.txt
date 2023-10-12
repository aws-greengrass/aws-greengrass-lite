cmake_minimum_required(VERSION 3.25)
project(iot_broker)

set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)

FetchContent_Declare(
  device-sdk-cpp
  GIT_REPOSITORY https://github.com/aws/aws-iot-device-sdk-cpp-v2.git
  GIT_TAG v1.29.0)
FetchContent_MakeAvailable(device-sdk-cpp)

add_library(iot_broker MODULE main.cpp)
target_link_libraries(iot_broker PRIVATE nucleus-core aws-crt-cpp)
