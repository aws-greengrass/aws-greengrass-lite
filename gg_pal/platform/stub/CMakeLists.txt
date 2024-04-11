add_subdirectory(../../../plugin_api plugin_api EXCLUDE_FROM_ALL)

add_library(
  gg_pal STATIC
  include/gg_pal/abstract_process.hpp
  include/gg_pal/abstract_process_manager.hpp
  include/gg_pal/env.hpp
  include/gg_pal/process.hpp
  include/gg_pal/process_manager.hpp
  include/gg_pal/startable.hpp
  src/startable.cpp)

target_include_directories(
  gg_pal
  PUBLIC include
  PRIVATE src)
target_link_libraries(
  gg_pal
  PRIVATE gg_plugin_api_min
  PUBLIC gg_pal_interface)
