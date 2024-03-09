cmake_minimum_required(VERSION 3.25)
project(ipc_server)

include(../../common.cmake)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

add_subdirectory(../device-sdk/ device-sdk EXCLUDE_FROM_ALL)

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

if(LINUX)
  target_link_options(
    ipc_server PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

install(TARGETS ipc_server DESTINATION plugins)
