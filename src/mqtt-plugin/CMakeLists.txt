cmake_minimum_required(VERSION 3.25)
project(mqtt-plugin)

include(FetchContent)

FetchContent_Declare(
  device-sdk-cpp
  GIT_REPOSITORY https://github.com/aws/aws-iot-device-sdk-cpp-v2.git
  GIT_TAG v1.29.0)
FetchContent_MakeAvailable(device-sdk-cpp)

add_library(mqtt-plugin SHARED mqtt-plugin.cpp)
target_link_libraries(mqtt-plugin PRIVATE nucleus-core aws-crt-cpp)
