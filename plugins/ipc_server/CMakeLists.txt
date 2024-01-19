cmake_minimum_required(VERSION 3.22)
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

add_library(
  ipc_server MODULE
  main.cpp
  src/ipc_server.cpp
  src/ipc_server.hpp
  src/server_bootstrap.cpp
  src/server_continuation.cpp
  src/server_continuation.hpp
  src/server_listener.cpp
  src/server_listener.hpp)
target_link_libraries(ipc_server PRIVATE nucleus-core aws-crt-cpp)
target_include_directories(ipc_server PRIVATE src)
if(UNIX AND NOT APPLE)
  target_link_options(
    ipc_server PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

install(TARGETS ipc_server DESTINATION plugins)
