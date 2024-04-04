cmake_minimum_required(VERSION 3.25)
project(fleet_status_service VERSION 0.1.0)

include(../../common.cmake)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

set(COMPONENT_NAME local.plugins.discovered.fleet_status_service)

enable_warnings()

add_library(fleet_status_service MODULE src/main.cpp)

target_link_libraries(fleet_status_service PRIVATE gg_plugin_api)

configureRPATH(fleet_status_service)

set_version_script(fleet_status_service
                   "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(
  TARGETS fleet_status_service
  DESTINATION plugins
  RUNTIME_DEPENDENCY_SET fleet_status_service_deps)

if(INSTALL_RUNTIME_DEPENDENCIES)
  install(RUNTIME_DEPENDENCY_SET fleet_status_service_deps)
endif()

install(FILES recipes/${COMPONENT_NAME}-${PROJECT_VERSION}.yaml
        DESTINATION plugins/recipes)
