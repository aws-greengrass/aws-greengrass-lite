cmake_minimum_required(VERSION 3.22)
project(gg_plugin_api)

add_library(
  gg_plugin_api
  INTERFACE
  include/buffer_stream.hpp
  include/c_api.h
  include/c_api.hpp
  include/cpp_api.hpp
  include/logging.hpp
  include/plugin.hpp
  include/util.hpp)

target_include_directories(gg_plugin_api INTERFACE include)
