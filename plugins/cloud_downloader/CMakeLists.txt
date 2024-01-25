cmake_minimum_required(VERSION 3.22)
project(Cloud_downloader)

include(FetchContent)
include(../../utils/fetchContentFromDeps.cmake)
find_package(CURL)

fetchContentFromDeps(device-sdk-cpp)

FetchContent_GetProperties(device-sdk-cpp)
if(NOT device-sdk-cpp_POPULATED)
  FetchContent_Populate(device-sdk-cpp)
  add_subdirectory(${device-sdk-cpp_SOURCE_DIR} ${device-sdk-cpp_BINARY_DIR}
                   EXCLUDE_FROM_ALL)
endif()
set(CURL_LIBRARY "-lcurl")

add_library(Cloud_downloader MODULE main.cpp)
target_link_libraries(Cloud_downloader PRIVATE nucleus-core aws-crt-cpp shared_resources CURL::libcurl)
if(UNIX AND NOT APPLE)
  target_link_options(
    Cloud_downloader PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

install(TARGETS Cloud_downloader DESTINATION plugins)
