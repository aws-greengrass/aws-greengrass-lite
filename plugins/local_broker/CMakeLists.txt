cmake_minimum_required(VERSION 3.25)
project(local_broker VERSION 0.1.0)

include(../../common.cmake)
set(COMPONENT_NAME local.plugins.discovered.local_broker)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)
add_subdirectory(../shared_resources shared_resources EXCLUDE_FROM_ALL)

enable_warnings()

add_library(local_broker MODULE src/main.cpp)

target_link_libraries(local_broker PRIVATE gg_plugin_api shared_resources)

configureRPATH(local_broker)

set_version_script(local_broker "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(
  TARGETS local_broker
  DESTINATION plugins
  RUNTIME_DEPENDENCY_SET local_broker_deps)

if(INSTALL_RUNTIME_DEPENDENCIES)
  install(RUNTIME_DEPENDENCY_SET local_broker_deps)
endif()

install(FILES recipes/${COMPONENT_NAME}-${PROJECT_VERSION}.yaml
        DESTINATION plugins/recipes)
