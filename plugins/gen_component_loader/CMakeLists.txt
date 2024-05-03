cmake_minimum_required(VERSION 3.25)
project(gen_component_loader VERSION 0.1.0)

include(../../common.cmake)
set(COMPONENT_NAME local.plugins.discovered.gen_component_loader)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)
add_subdirectory(../../gg_pal gg_pal EXCLUDE_FROM_ALL)
add_subdirectory(../device-sdk/ device-sdk EXCLUDE_FROM_ALL)

enable_warnings()

# Object library shared between plugin and tests

add_library(gen_component_loader_obj OBJECT src/gen_component_loader.cpp
                                            src/gen_component_loader.hpp)

target_include_directories(gen_component_loader_obj PUBLIC src)
target_link_libraries(gen_component_loader_obj PUBLIC gg_plugin_api gg_pal
                                                      device-sdk)

# Plugin

add_library(gen_component_loader MODULE src/main.cpp)
target_link_libraries(gen_component_loader PRIVATE gen_component_loader_obj)

configureRPATH(gen_component_loader)

set_version_script(gen_component_loader
                   "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(
  TARGETS gen_component_loader
  DESTINATION plugins
  RUNTIME_DEPENDENCY_SET gen_component_loader_deps)

if(INSTALL_RUNTIME_DEPENDENCIES)
  install(RUNTIME_DEPENDENCY_SET gen_component_loader_deps)
endif()

install(FILES recipes/${COMPONENT_NAME}-${PROJECT_VERSION}.yaml
        DESTINATION plugins/recipes)
# Tests

if(BUILD_TESTING AND NOT WIN32)
  include(CTest)

  fetchContentAddCmake(Catch2 trompeloeil)

  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)

  add_subdirectory(../../nucleus nucleus EXCLUDE_FROM_ALL)

  add_executable(gen_component_loader_tests tests/test_gen_component_loader.cpp
                                            tests/test_gen_component_loader.cpp)

  target_include_directories(gen_component_loader_tests PRIVATE tests)
  target_link_libraries(
    gen_component_loader_tests
    PRIVATE gen_component_loader_obj Catch2::Catch2WithMain
            trompeloeil::trompeloeil nucleus-core shared_test_resources)

  catch_discover_tests(gen_component_loader_tests)

  add_test(NAME gen_component_loader COMMAND gen_component_loader_tests)
endif()
