cmake_minimum_required(VERSION 3.22)
project(tes_http_server_plugin)

include(FetchContent)
include(../../utils/fetchContentFromDeps.cmake)

FetchContent_GetProperties(device-sdk-cpp)
if(NOT device-sdk-cpp_POPULATED)
  FetchContent_Populate(device-sdk-cpp)
  add_subdirectory(${device-sdk-cpp_SOURCE_DIR} ${device-sdk-cpp_BINARY_DIR}
                   EXCLUDE_FROM_ALL)
endif()

add_library(tes_http_server_plugin MODULE main.cpp src/tes_http_server.cpp
                                          src/tes_http_server.hpp)
target_link_libraries(tes_http_server_plugin PRIVATE nucleus-core aws-c-http)
target_include_directories(
  tes_http_server_plugin
  PUBLIC include
  PRIVATE src)
install(TARGETS tes_http_server_plugin DESTINATION plugins)
