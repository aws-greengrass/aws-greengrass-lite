include_guard(GLOBAL)
cmake_minimum_required(VERSION 3.25)
project(gg_plugin_api)

include(../common.cmake)

add_library(
  gg_plugin_api_min
  INTERFACE
  include/api_archive.hpp
  include/api_callbacks.hpp
  include/api_errors.hpp
  include/api_forwards.hpp
  include/api_logging.hpp
  include/api_standard_errors.hpp
  include/archive.hpp
  include/atomic_shared.hpp
  include/auto_release.hpp
  include/buffer_stream.hpp
  include/c_api.h
  include/c_api.hpp
  include/channel.hpp
  include/checked_pointers.hpp
  include/containers.hpp
  include/cpp_api.hpp
  include/error_tmpl.hpp
  include/futures.hpp
  include/greengrass_traits.hpp
  include/handles.hpp
  include/ipc_standard_errors.hpp
  include/logging.hpp
  include/lookup_table.hpp
  include/plugin.hpp
  include/ref_object.hpp
  include/scopes.hpp
  include/span.hpp
  include/string_util.hpp
  include/subscriptions.hpp
  include/temp_module.hpp
  include/util.hpp)

target_include_directories(gg_plugin_api_min INTERFACE include)

add_library(gg_plugin_api INTERFACE)
target_link_libraries(gg_plugin_api INTERFACE gg_plugin_api_min)

if(NOT LINUX)
  target_link_libraries(gg_plugin_api INTERFACE nucleus-core)
endif()
