cmake_minimum_required(VERSION 3.22)
project(cli_server)

add_library(
  cli_server MODULE
  main.cpp
  src/cli_server.cpp
  src/cli_server.hpp
  src/darwin_platform.hpp
  src/platform.hpp
  src/unix_platform.hpp
  src/windows_platform.hpp)
target_link_libraries(cli_server PRIVATE nucleus-core)
target_include_directories(cli_server PRIVATE src)

install(TARGETS cli_server DESTINATION plugins)
