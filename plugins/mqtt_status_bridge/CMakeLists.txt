cmake_minimum_required(VERSION 3.25)
project(mqtt_status_bridge VERSION 0.1.0)

include(../../common.cmake)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

set(COMPONENT_NAME local.plugins.discovered.mqtt_status_bridge)

enable_warnings()

add_library(mqtt_status_bridge MODULE src/main.cpp)

target_link_libraries(mqtt_status_bridge PRIVATE gg_plugin_api)

configureRPATH(mqtt_status_bridge)

set_version_script(mqtt_status_bridge
                   "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(
  TARGETS mqtt_status_bridge
  DESTINATION plugins
  RUNTIME_DEPENDENCY_SET mqtt_status_bridge_deps)

if(INSTALL_RUNTIME_DEPENDENCIES)
  install(RUNTIME_DEPENDENCY_SET mqtt_status_bridge_deps)
endif()

install(FILES recipes/${COMPONENT_NAME}-${PROJECT_VERSION}.yaml
        DESTINATION plugins/recipes)
