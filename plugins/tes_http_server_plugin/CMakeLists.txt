cmake_minimum_required(VERSION 3.25)
project(tes_http_server_plugin VERSION 0.1.0)

include(../../common.cmake)
set(COMPONENT_NAME local.plugins.discovered.tes_http_server_plugin)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

add_subdirectory(../device-sdk/ device-sdk EXCLUDE_FROM_ALL)

enable_warnings()

add_library(tes_http_server_plugin MODULE src/main.cpp src/tes_http_server.cpp
                                          src/tes_http_server.hpp)

target_link_libraries(tes_http_server_plugin PRIVATE gg_plugin_api device-sdk)

configureRPATH(tes_http_server_plugin)

set_version_script(tes_http_server_plugin
                   "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(TARGETS tes_http_server_plugin DESTINATION plugins)
install(FILES recipes/${COMPONENT_NAME}-${PROJECT_VERSION}.yaml
        DESTINATION plugins/recipes)
