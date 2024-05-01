cmake_minimum_required(VERSION 3.25)
project(authorization_handler VERSION 0.1.0)

include(../../common.cmake)
set(COMPONENT_NAME local.plugins.discovered.authorization_handler)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

enable_warnings()

add_library(authorization_handler MODULE
    src/authorization_handler.cpp
    src/authorization_handler.hpp
    src/authorization_module.cpp
    src/authorization_module.hpp
    src/authorization_policy_parser.cpp
    src/authorization_policy.hpp
    src/main.cpp
    src/permission.hpp
    src/wildcard_trie.hpp)

target_link_libraries(authorization_handler PRIVATE gg_plugin_api)

configureRPATH(authorization_handler)

set_version_script(authorization_handler
                    "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(
  TARGETS authorization_handler
  DESTINATION plugins)

install(FILES recipes/${COMPONENT_NAME}-${PROJECT_VERSION}.yaml
        DESTINATION plugins/recipes)
