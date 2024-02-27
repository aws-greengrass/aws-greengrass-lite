cmake_minimum_required(VERSION 3.22)
project(cloud_downloader)

include(../../utils/fetchContentFromDeps.cmake)

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

fetchContentAddCmake(device-sdk-cpp)

add_library(cloud_downloader_obj OBJECT src/cloud_downloader.cpp
                                        src/cloud_downloader.hpp)
target_link_libraries(cloud_downloader_obj PRIVATE gg_plugin_api aws-crt-cpp
                                                   shared_resources)
target_include_directories(cloud_downloader_obj PUBLIC src)
set_property(TARGET cloud_downloader_obj PROPERTY POSITION_INDEPENDENT_CODE 1)

##Build for shared
add_library(cloud_downloader MODULE src/main.cpp)
target_link_libraries(
  cloud_downloader PRIVATE cloud_downloader_obj gg_plugin_api aws-crt-cpp
                           shared_resources)
target_include_directories(cloud_downloader PRIVATE src)
install(TARGETS cloud_downloader DESTINATION plugins)

if(LINUX)
  target_link_options(
    cloud_downloader PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

##Build for Testing
if(BUILD_TESTING)
  include(CTest)

  fetchContentAddCmake(Catch2 trompeloeil)

  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)

  add_library(cloud_downloader_lib STATIC)
  target_link_libraries(
    cloud_downloader_lib PUBLIC cloud_downloader_obj nucleus-lib aws-crt-cpp
                                shared_resources)
  target_include_directories(cloud_downloader_lib PUBLIC src)

  add_executable(
    cloud_downloader_tests tests/test_cloud_downloader.cpp
                           tests/test_cloud_downloader.cpp tests/test_util.hpp)
  target_link_libraries(
    cloud_downloader_tests PUBLIC cloud_downloader_lib Catch2::Catch2WithMain
                                  trompeloeil::trompeloeil)
  target_include_directories(cloud_downloader_tests PRIVATE tests)
  catch_discover_tests(cloud_downloader_tests)
endif()
