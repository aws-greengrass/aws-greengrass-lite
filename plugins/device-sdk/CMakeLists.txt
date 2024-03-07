include_guard(GLOBAL)
cmake_minimum_required(VERSION 3.22)
project(device-sdk)

include(../../common.cmake)

if(UNIX)
  add_compile_options(-fvisibility=default)
endif()

fetchContentAddCmake(device-sdk-cpp)

add_library(device-sdk SHARED src/main.cpp)
target_link_libraries(device-sdk PUBLIC aws-crt-cpp IotIdentity-cpp aws-c-http)

install(TARGETS device-sdk DESTINATION lib)
