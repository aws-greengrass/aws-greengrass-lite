cmake_minimum_required(VERSION 3.25)
project(cli_server VERSION 0.1.0)

include(../../common.cmake)
set(COMPONENT_NAME local.plugins.discovered.cli_server)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

enable_warnings()

add_library(cli_server MODULE src/cli_server.cpp src/cli_server.hpp
                              src/main.cpp)

target_link_libraries(cli_server PRIVATE gg_plugin_api)

set_version_script(cli_server "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(TARGETS cli_server DESTINATION plugins)
install(FILES recipes/${COMPONENT_NAME}-${PROJECT_VERSION}.yaml
        DESTINATION plugins/recipes)
