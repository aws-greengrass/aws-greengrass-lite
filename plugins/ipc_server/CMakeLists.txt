cmake_minimum_required(VERSION 3.25)
project(ipc_server)

include(FetchContent)
include(../../utils/fetchContentFromDeps.cmake)

fetchContentFromDeps(device-sdk-cpp)

FetchContent_GetProperties(device-sdk-cpp)
if(NOT device-sdk-cpp_POPULATED)
  FetchContent_Populate(device-sdk-cpp)
  add_subdirectory(${device-sdk-cpp_SOURCE_DIR} ${device-sdk-cpp_BINARY_DIR}
                   EXCLUDE_FROM_ALL)
endif()

add_library(ipc_server MODULE main.cpp)
target_link_libraries(ipc_server PRIVATE nucleus-core aws-crt-cpp)

install(TARGETS ipc_server DESTINATION plugins)
