cmake_minimum_required(VERSION 3.25)
project(log_manager VERSION 1.0.0)

include(../../common.cmake)
set(COMPONENT_NAME local.plugins.discovered.log_manager)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
    message(
            SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

enable_warnings()

add_library(
        log_manager MODULE
        src/main.cpp
        src/log_manager.cpp
        src/log_manager.hpp
)

target_link_libraries(log_manager PRIVATE gg_plugin_api)

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
