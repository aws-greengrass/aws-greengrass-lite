include_guard(GLOBAL)
cmake_minimum_required(VERSION 3.22)
project(gg_plugin_api)

include(../common.cmake)

add_library(
  gg_plugin_api_min
  INTERFACE
  include/api_callbacks.hpp
  include/api_errors.hpp
  include/api_forwards.hpp
  include/api_logging.hpp
  include/buffer_stream.hpp
  include/c_api.h
  include/c_api.hpp
  include/channel.hpp
  include/containers.hpp
  include/cpp_api.hpp
  include/handles.hpp
  include/logging.hpp
  include/plugin.hpp
  include/scopes.hpp
  include/tasks_subscriptions.hpp
  include/util.hpp
  include/error_tmpl.hpp
  include/atomic_shared.hpp)

target_include_directories(gg_plugin_api_min INTERFACE include)

add_library(gg_plugin_api INTERFACE)
target_link_libraries(gg_plugin_api INTERFACE gg_plugin_api_min)

if(NOT LINUX)
  target_link_libraries(gg_plugin_api INTERFACE nucleus-core)
endif()
