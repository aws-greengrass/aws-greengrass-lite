cmake_minimum_required(VERSION 3.25)
project(gen_component_loader)

include(../../common.cmake)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

add_subdirectory(../device-sdk/ device-sdk EXCLUDE_FROM_ALL)

enable_warnings()

# Object library shared between plugin and tests

add_library(
  gen_component_loader_obj OBJECT
  src/gen_component_loader.cpp
  src/gen_component_loader.hpp
  src/platform_abstraction/abstract_process.hpp
  src/platform_abstraction/abstract_process_manager.hpp
  src/platform_abstraction/env.hpp
  src/platform_abstraction/startable.hpp)

if(LINUX)
  target_sources(
    gen_component_loader_obj
    PRIVATE src/platform_abstraction/linux/error.hpp
            src/platform_abstraction/linux/file_descriptor.cpp
            src/platform_abstraction/linux/file_descriptor.hpp
            src/platform_abstraction/linux/permissions.cpp
            src/platform_abstraction/linux/permissions.hpp
            src/platform_abstraction/linux/pipe.cpp
            src/platform_abstraction/linux/pipe.hpp
            src/platform_abstraction/linux/process.cpp
            src/platform_abstraction/linux/process.hpp
            src/platform_abstraction/linux/process_manager.cpp
            src/platform_abstraction/linux/process_manager.hpp
            src/platform_abstraction/linux/rlimits.cpp
            src/platform_abstraction/linux/rlimits.hpp
            src/platform_abstraction/linux/startable.cpp
            src/platform_abstraction/linux/syscall.hpp)
else()
  target_sources(
    gen_component_loader_obj
    PRIVATE src/platform_abstraction/stub/startable.cpp
            src/platform_abstraction/stub/process_manager.hpp
            src/platform_abstraction/stub/process.hpp)
endif()

target_include_directories(gen_component_loader_obj PUBLIC src)
target_link_libraries(gen_component_loader_obj PUBLIC gg_plugin_api device-sdk)

# Plugin

add_library(gen_component_loader MODULE src/main.cpp)
target_link_libraries(gen_component_loader PRIVATE gen_component_loader_obj)

configureRPATH(gen_component_loader)

set_version_script(gen_component_loader
                   "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(TARGETS gen_component_loader DESTINATION plugins)

# Tests

if(BUILD_TESTING AND NOT WIN32)
  include(CTest)

  fetchContentAddCmake(Catch2 trompeloeil)

  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)

  add_subdirectory(../../nucleus nucleus EXCLUDE_FROM_ALL)

  add_executable(
    gen_component_loader_tests
    tests/test_gen_component_loader.cpp tests/test_gen_component_loader.cpp
    tests/test_util.hpp)

  target_include_directories(gen_component_loader_tests PRIVATE tests)
  target_link_libraries(
    gen_component_loader_tests
    PRIVATE gen_component_loader_obj Catch2::Catch2WithMain
            trompeloeil::trompeloeil nucleus-core)

  catch_discover_tests(gen_component_loader_tests)

  add_test(NAME gen_component_loader COMMAND gen_component_loader_tests)
endif()
