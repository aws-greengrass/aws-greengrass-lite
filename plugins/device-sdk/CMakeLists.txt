include_guard(GLOBAL)
cmake_minimum_required(VERSION 3.25)
project(device-sdk)

include(../../common.cmake)

if(UNIX)
  add_compile_options(-fvisibility=default)
endif()

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
cmake_policy(SET CMP0077 NEW)

fetchContentAddCmake(device-sdk-cpp)

add_library(device-sdk SHARED include/shared_device_sdk.hpp src/main.cpp)

if(APPLE)
  target_link_libraries(
    device-sdk PRIVATE -Wl,-all_load aws-crt-cpp IotIdentity-cpp aws-c-http
                       aws-c-event-stream -Wl,-noall_load)
else()
  target_link_libraries(
    device-sdk PRIVATE -Wl,--whole-archive aws-crt-cpp IotIdentity-cpp
                       aws-c-http aws-c-event-stream -Wl,--no-whole-archive)
endif()

target_include_directories(
  device-sdk
  PUBLIC include
         $<TARGET_PROPERTY:aws-crt-cpp,INTERFACE_INCLUDE_DIRECTORIES>
         $<TARGET_PROPERTY:IotIdentity-cpp,INTERFACE_INCLUDE_DIRECTORIES>
         $<TARGET_PROPERTY:aws-c-http,INTERFACE_INCLUDE_DIRECTORIES>
         $<TARGET_PROPERTY:aws-c-event-stream,INTERFACE_INCLUDE_DIRECTORIES>)

install(TARGETS device-sdk DESTINATION lib)
