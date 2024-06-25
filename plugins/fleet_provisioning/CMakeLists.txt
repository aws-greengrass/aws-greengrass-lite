cmake_minimum_required(VERSION 3.25)
project(fleet_provisioning VERSION 0.1.0)

include(../../common.cmake)
set(COMPONENT_NAME aws.greengrass.fleet_provisioning)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

add_subdirectory(../device-sdk/ device-sdk EXCLUDE_FROM_ALL)

enable_warnings()

add_library(fleet_provisioning MODULE src/fleet_provisioning.cpp
                                      src/fleet_provisioning.hpp src/main.cpp)

target_link_libraries(fleet_provisioning PRIVATE gg_plugin_api device-sdk)

configureRPATH(fleet_provisioning)

set_version_script(fleet_provisioning
                   "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(
  TARGETS fleet_provisioning
  DESTINATION plugins
  RUNTIME_DEPENDENCY_SET fleet_provisioning_deps)

if(INSTALL_RUNTIME_DEPENDENCIES)
  install(RUNTIME_DEPENDENCY_SET fleet_provisioning_deps)
endif()

install(FILES recipes/${COMPONENT_NAME}-${PROJECT_VERSION}.yaml
        DESTINATION plugins/recipes)
