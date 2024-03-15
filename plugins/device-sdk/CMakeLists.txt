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

add_library(device-sdk SHARED src/main.cpp)
target_link_libraries(device-sdk PUBLIC aws-crt-cpp IotIdentity-cpp aws-c-http)

install(TARGETS device-sdk DESTINATION lib)
