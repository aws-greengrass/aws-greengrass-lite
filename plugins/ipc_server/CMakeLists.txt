cmake_minimum_required(VERSION 3.25)
project(ipc_server VERSION 0.1.0)

include(../../common.cmake)
set(COMPONENT_NAME local.plugins.discovered.ipc_server)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

add_subdirectory(../device-sdk/ device-sdk EXCLUDE_FROM_ALL)

enable_warnings()

# Object library shared between plugin and tests

add_library(
  ipc_server_obj OBJECT
  src/authentication_handler.cpp
  src/authentication_handler.hpp
  src/bound_promise.hpp
  src/connection_stream.cpp
  src/connection_stream.hpp
  src/ipc_server.cpp
  src/ipc_server.hpp
  src/message.cpp
  src/message.hpp
  src/random_device.cpp
  src/random_device.hpp
  src/server_bootstrap.cpp
  src/server_bootstrap.hpp
  src/server_connection.cpp
  src/server_connection.hpp
  src/server_listener.cpp
  src/server_listener.hpp)

target_include_directories(ipc_server_obj PUBLIC src)
target_link_libraries(ipc_server_obj PUBLIC gg_plugin_api device-sdk
                                            shared_resources)

# Plugin

add_library(ipc_server MODULE src/main.cpp)
target_link_libraries(ipc_server PRIVATE ipc_server_obj)

configureRPATH(ipc_server)

set_version_script(ipc_server "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(
  TARGETS ipc_server
  DESTINATION plugins
  RUNTIME_DEPENDENCY_SET ipc_server_deps)

if(INSTALL_RUNTIME_DEPENDENCIES)
  install(RUNTIME_DEPENDENCY_SET ipc_server_deps)
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

  add_executable(ipc_server_tests tests/test_ipc_server.cpp)

  target_include_directories(ipc_server_tests PRIVATE tests)
  target_link_libraries(
    ipc_server_tests
    PRIVATE ipc_server_obj Catch2::Catch2WithMain trompeloeil::trompeloeil
            nucleus-core shared_test_resources GreengrassIpc-cpp)

  catch_discover_tests(ipc_server_tests)

  add_test(NAME ipc_server COMMAND ipc_server_tests)
endif()
