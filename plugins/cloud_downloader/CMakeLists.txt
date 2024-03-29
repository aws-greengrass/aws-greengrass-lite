cmake_minimum_required(VERSION 3.25)
project(cloud_downloader VERSION 0.1.0)

include(../../common.cmake)
set(COMPONENT_NAME local.plugins.discovered.cloud_downloader)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

add_subdirectory(../device-sdk/ device-sdk EXCLUDE_FROM_ALL)

enable_warnings()

# Object library shared between plugin and tests

add_library(cloud_downloader_obj OBJECT src/cloud_downloader.cpp
                                        src/cloud_downloader.hpp)

target_include_directories(cloud_downloader_obj PUBLIC src)
target_link_libraries(cloud_downloader_obj PUBLIC gg_plugin_api device-sdk)

# Plugin

add_library(cloud_downloader MODULE src/main.cpp)
target_link_libraries(cloud_downloader PRIVATE cloud_downloader_obj)

configureRPATH(cloud_downloader)

set_version_script(cloud_downloader
                   "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(TARGETS cloud_downloader DESTINATION plugins)
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
    cloud_downloader_tests tests/test_cloud_downloader.cpp
                           tests/test_cloud_downloader.cpp tests/test_util.hpp)

  target_include_directories(cloud_downloader_tests PRIVATE tests)
  target_link_libraries(
    cloud_downloader_tests PRIVATE cloud_downloader_obj Catch2::Catch2WithMain
                                   trompeloeil::trompeloeil nucleus-core)

  catch_discover_tests(cloud_downloader_tests)

  add_test(NAME cloud_downloader COMMAND cloud_downloader_tests)
endif()
