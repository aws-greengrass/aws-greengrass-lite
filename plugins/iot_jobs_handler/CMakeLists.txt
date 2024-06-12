cmake_minimum_required(VERSION 3.25)
project(iot_jobs_handler VERSION 0.1.0)

include(../../common.cmake)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

set(COMPONENT_NAME local.plugins.discovered.iot_jobs_handler)

enable_warnings()

add_library(iot_jobs_handler MODULE src/iot_jobs_handler.hpp src/main.cpp)
target_link_libraries(iot_jobs_handler PRIVATE gg_plugin_api)

configureRPATH(iot_jobs_handler)

set_version_script(iot_jobs_handler
                   "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(
  TARGETS iot_jobs_handler
  DESTINATION plugins
  RUNTIME_DEPENDENCY_SET iot_jobs_handler_deps)

if(INSTALL_RUNTIME_DEPENDENCIES)
  install(RUNTIME_DEPENDENCY_SET iot_jobs_handler_deps)
endif()

install(FILES recipes/${COMPONENT_NAME}-${PROJECT_VERSION}.yaml
        DESTINATION plugins/recipes)
