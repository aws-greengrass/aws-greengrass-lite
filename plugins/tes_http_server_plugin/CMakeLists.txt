cmake_minimum_required(VERSION 3.22)
project(tes_http_server_plugin)

include(../../utils/fetchContentFromDeps.cmake)

fetchContentAddCmake(device-sdk-cpp)

add_library(tes_http_server_plugin MODULE main.cpp src/tes_http_server.cpp
                                          src/tes_http_server.hpp)
target_link_libraries(tes_http_server_plugin PRIVATE nucleus-core aws-c-http)
target_include_directories(
  tes_http_server_plugin
  PUBLIC include
  PRIVATE src)
install(TARGETS tes_http_server_plugin DESTINATION plugins)
