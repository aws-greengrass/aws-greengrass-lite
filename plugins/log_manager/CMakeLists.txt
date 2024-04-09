cmake_minimum_required(VERSION 3.25)
project(log_manager VERSION 0.1.0)

include(../../common.cmake)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
          SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)
add_subdirectory(../device-sdk/ device-sdk EXCLUDE_FROM_ALL)
#add_subdirectory(../aws-sdk-cpp/ aws-sdk-cpp EXCLUDE_FROM_ALL)

set(COMPONENT_NAME local.plugins.discovered.log_manager)

enable_warnings()

# Object library shared between plugin and tests

add_library(log_manager_obj OBJECT src/log_manager.cpp src/log_manager.hpp
        src/main.cpp)

target_include_directories(log_manager_obj PUBLIC src)
target_link_libraries(log_manager_obj PUBLIC gg_plugin_api device-sdk)
#target_link_libraries(log_manager_obj PUBLIC gg_plugin_api device-sdk aws-sdk-cpp)

# Plugin

add_library(log_manager MODULE src/main.cpp)
target_link_libraries(log_manager PRIVATE log_manager_obj)

configureRPATH(log_manager)

set_version_script(log_manager "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(
        TARGETS log_manager
        DESTINATION plugins
        RUNTIME_DEPENDENCY_SET log_manager_deps)

if(INSTALL_RUNTIME_DEPENDENCIES)
  install(RUNTIME_DEPENDENCY_SET log_manager_deps)
endif()

install(FILES recipes/${COMPONENT_NAME}-${PROJECT_VERSION}.yaml
        DESTINATION plugins/recipes)

# Tests

if(BUILD_TESTING AND NOT WIN32)
  include(CTest)

  fetchContentAddCmake(Catch2 trompeloeil)

  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)

  add_subdirectory(../../nucleus nucleus EXCLUDE_FROM_ALL)

  add_executable(
          log_manager_tests tests/test_log_manager.cpp
          tests/test_log_manager.cpp tests/test_util.hpp)

  target_include_directories(log_manager_tests PRIVATE tests)
  target_link_libraries(
          log_manager_tests PRIVATE log_manager_obj Catch2::Catch2WithMain
          trompeloeil::trompeloeil nucleus-core)

  catch_discover_tests(log_manager_tests)

  add_test(NAME log_manager COMMAND log_manager_tests)
endif()
