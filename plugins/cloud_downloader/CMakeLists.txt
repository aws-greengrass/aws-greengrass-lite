cmake_minimum_required(VERSION 3.22)
project(Cloud_downloader)

include(FetchContent)
include(../../utils/fetchContentFromDeps.cmake)

fetchContentFromDeps(device-sdk-cpp)

FetchContent_GetProperties(device-sdk-cpp)
if(NOT device-sdk-cpp_POPULATED)
  FetchContent_Populate(device-sdk-cpp)
  add_subdirectory(${device-sdk-cpp_SOURCE_DIR} ${device-sdk-cpp_BINARY_DIR}
                   EXCLUDE_FROM_ALL)
endif()

add_library(Cloud_downloader MODULE main.cpp)
target_link_libraries(Cloud_downloader PRIVATE nucleus-core aws-crt-cpp
                                               shared_resources )

install(TARGETS Cloud_downloader DESTINATION plugins)