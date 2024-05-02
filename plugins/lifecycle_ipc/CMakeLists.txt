cmake_minimum_required(VERSION 3.25)
project(lifecycle_ipc VERSION 0.1.0)

include(../../common.cmake)
set(COMPONENT_NAME local.plugins.discovered.lifecycle_ipc)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)
add_subdirectory(../shared_resources shared_resources EXCLUDE_FROM_ALL)

add_subdirectory(../device-sdk/ device-sdk EXCLUDE_FROM_ALL)

enable_warnings()

# Object library shared between plugin and tests

add_library(lifecycle_ipc_obj OBJECT src/lifecycle_ipc.cpp
                                     src/lifecycle_ipc.hpp)

target_include_directories(lifecycle_ipc_obj PUBLIC src)
target_link_libraries(lifecycle_ipc_obj PUBLIC gg_plugin_api device-sdk)

add_library(lifecycle_ipc MODULE src/lifecycle_ipc.cpp src/lifecycle_ipc.hpp
                                 src/main.cpp)

target_link_libraries(lifecycle_ipc PRIVATE gg_plugin_api device-sdk
                                            shared_resources)

configureRPATH(lifecycle_ipc)

set_version_script(lifecycle_ipc "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(
  TARGETS lifecycle_ipc
  DESTINATION plugins
  RUNTIME_DEPENDENCY_SET lifecycle_ipc_deps)

if(INSTALL_RUNTIME_DEPENDENCIES)
  install(RUNTIME_DEPENDENCY_SET lifecycle_ipc_deps)
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

  add_executable(lifecycle_ipc_tests tests/test_lifecycle_ipc.cpp)

  target_include_directories(lifecycle_ipc_tests PRIVATE tests)
  target_link_libraries(
    lifecycle_ipc_tests
    PRIVATE lifecycle_ipc_obj Catch2::Catch2WithMain trompeloeil::trompeloeil
            nucleus-core shared_test_resources GreengrassIpc-cpp)

  catch_discover_tests(lifecycle_ipc_tests)

  add_test(NAME lifecycle_ipc COMMAND lifecycle_ipc_tests)
endif()
