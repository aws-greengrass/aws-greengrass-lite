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

add_library(
  authorization_handler_obj OBJECT
  src/authorization_handler.cpp
  src/authorization_handler.hpp
  src/authorization_module.cpp
  src/authorization_module.hpp
  src/authorization_policy.hpp
  src/authorization_policy_parser.cpp
  src/permission.hpp
  src/wildcard_trie.hpp)

target_include_directories(authorization_handler_obj PUBLIC src)
target_link_libraries(authorization_handler_obj PUBLIC gg_plugin_api)

add_library(authorization_handler MODULE src/main.cpp)
target_link_libraries(authorization_handler PRIVATE authorization_handler_obj)

configureRPATH(authorization_handler)

set_version_script(authorization_handler
                   "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(TARGETS authorization_handler DESTINATION plugins)

install(FILES recipes/${COMPONENT_NAME}-${PROJECT_VERSION}.yaml
        DESTINATION plugins/recipes)

# Tests

if(BUILD_TESTING AND NOT WIN32)
    include(CTest)

    fetchContentAddCmake(Catch2 trompeloeil)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)

    add_subdirectory(../../nucleus nucleus EXCLUDE_FROM_ALL)

    add_executable(
    authorization_handler_tests tests/test_authorization_handler.cpp)

    target_include_directories(authorization_handler_tests PRIVATE tests)
    target_link_libraries(
        authorization_handler_tests PRIVATE authorization_handler_obj Catch2::Catch2WithMain
                                    trompeloeil::trompeloeil nucleus-core shared_test_resources)

    catch_discover_tests(authorization_handler_tests)

    add_test(NAME authorization_handler COMMAND authorization_handler_tests)
endif()
