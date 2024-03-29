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

add_library(
  ipc_server MODULE
  src/authentication_handler.cpp
  src/authentication_handler.cpp
  src/authentication_handler.hpp
  src/authentication_handler.hpp
  src/ipc_server.cpp
  src/ipc_server.hpp
  src/main.cpp
  src/server_bootstrap.cpp
  src/server_continuation.cpp
  src/server_continuation.hpp
  src/server_listener.cpp
  src/server_listener.hpp)

target_link_libraries(ipc_server PRIVATE gg_plugin_api device-sdk)

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
