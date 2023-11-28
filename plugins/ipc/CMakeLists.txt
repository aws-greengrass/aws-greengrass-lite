cmake_minimum_required(VERSION 3.25)
project(ipc)

include(../../utils/fetchContentFromDeps.cmake)
fetchContentFromDeps(device-sdk-cpp)

FetchContent_GetProperties(device-sdk-cpp)
if(NOT device-sdk-cpp_POPULATED)
  FetchContent_Populate(device-sdk-cpp)
  add_subdirectory(${device-sdk-cpp_SOURCE_DIR} ${device-sdk-cpp_BINARY_DIR}
                   EXCLUDE_FROM_ALL)
endif()

add_library(
  ipc MODULE
  include/event_stream.hpp
  include/ipc_plugin.hpp
  include/rpc_server.hpp
  include/server_connection.hpp
  include/server_listener.hpp
  main.cpp
  src/basic_server.cpp
  src/event_stream.cpp
  src/ipc_plugin.cpp
  src/rpc_server.cpp
  src/server_connection.cpp
  src/server_listener.cpp)
target_link_libraries(ipc PRIVATE nucleus-core aws-crt-cpp)
target_include_directories(ipc PRIVATE include)
install(TARGETS ipc DESTINATION plugins)
