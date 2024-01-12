cmake_minimum_required(VERSION 3.22)
project(sample-component)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(FETCHCONTENT_QUIET FALSE)
include(FetchContent)
include(../utils/fetchContentFromDeps.cmake)

fetchcontentfromdeps(device-sdk-cpp)

FetchContent_GetProperties(device-sdk-cpp)
if(NOT device-sdk-cpp_POPULATED)
  FetchContent_Populate(device-sdk-cpp)
  add_subdirectory(${device-sdk-cpp_SOURCE_DIR} ${device-sdk-cpp_BINARY_DIR}
                   EXCLUDE_FROM_ALL)
endif()

add_executable(sample-component main.cpp)

install(TARGETS sample-component DESTINATION bin)

target_link_libraries(sample-component PRIVATE GreengrassIpc-cpp)
