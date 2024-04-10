add_subdirectory(../../../plugin_api plugin_api EXCLUDE_FROM_ALL)

add_library(
  gg_pal STATIC
  include/gg_pal/file_descriptor.hpp
  include/gg_pal/permissions.hpp
  include/gg_pal/process.hpp
  include/gg_pal/process_manager.hpp
  src/error.hpp
  src/file_descriptor.cpp
  src/permissions.cpp
  src/pipe.cpp
  src/pipe.hpp
  src/process.cpp
  src/process_manager.cpp
  src/rlimits.cpp
  src/rlimits.hpp
  src/startable.cpp
  src/syscall.hpp)

target_include_directories(
  gg_pal
  PUBLIC include
  PRIVATE src)
target_link_libraries(
  gg_pal
  PRIVATE gg_plugin_api_min
  PUBLIC gg_pal_interface)
