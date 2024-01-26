cmake_minimum_required(VERSION 3.22)
project(native_plugin)

if(UNIX OR APPLE)
  add_library(native_plugin MODULE main.cpp posix/startable.cpp)
  target_link_libraries(native_plugin PRIVATE nucleus-core)
  install(TARGETS native_plugin DESTINATION plugins)
endif()
