cmake_minimum_required(VERSION 3.22)
project(native_plugin)

if(UNIX
   AND NOT APPLE
   AND NOT WIN32)
  add_library(
    native_plugin_obj OBJECT
    src/native_plugin.cpp
    src/native_plugin.hpp
    src/env.hpp
    src/abstract_process.hpp
    src/abstract_process_manager.hpp
    src/startable.hpp
    src/linux/startable.cpp
    src/linux/process.cpp
    src/linux/process.hpp
    src/linux/file_descriptor.cpp
    src/linux/file_descriptor.hpp
    src/linux/process_manager.cpp
    src/linux/process_manager.hpp
    src/linux/rlimits.hpp
    src/linux/rlimits.cpp
    src/linux/pipe.cpp
    src/linux/pipe.hpp
    src/linux/permissions.hpp
    src/linux/permissions.cpp
    src/linux/error.hpp)
  target_link_libraries(native_plugin_obj PRIVATE gg_plugin_api)
  target_include_directories(native_plugin_obj PUBLIC src)
  set_property(TARGET native_plugin_obj PROPERTY POSITION_INDEPENDENT_CODE 1)

  add_library(native_plugin MODULE $<TARGET_OBJECTS:native_plugin_obj>
                                   src/main.cpp)
  target_include_directories(native_plugin PRIVATE src)
  target_link_libraries(native_plugin PRIVATE nucleus-core)
  target_link_options(
    native_plugin PUBLIC
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
  install(TARGETS native_plugin DESTINATION plugins)

  add_library(native_plugin_lib STATIC $<TARGET_OBJECTS:native_plugin_obj>)
  target_link_libraries(native_plugin_lib PUBLIC nucleus-lib)
  target_include_directories(native_plugin_lib PUBLIC src)

  add_executable(
    native_plugin_tests tests/test_native_plugin.cpp
                        tests/test_process_manager.cpp tests/test_util.hpp)
  target_link_libraries(
    native_plugin_tests PUBLIC native_plugin_lib Catch2::Catch2WithMain
                               trompeloeil::trompeloeil)
  target_include_directories(native_plugin_tests PRIVATE tests)
  catch_discover_tests(native_plugin_tests)
endif()
