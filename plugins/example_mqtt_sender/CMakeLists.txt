cmake_minimum_required(VERSION 3.22)
project(example_mqtt_sender)

include(../../common.cmake)

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

# Object library shared between plugin and tests

add_library(example_mqtt_sender_obj OBJECT src/example_mqtt_sender.cpp
                                           src/example_mqtt_sender.hpp)

target_include_directories(example_mqtt_sender_obj PUBLIC src)
target_link_libraries(example_mqtt_sender_obj PUBLIC gg_plugin_api)

set_property(TARGET example_mqtt_sender_obj PROPERTY POSITION_INDEPENDENT_CODE
                                                     1)

# Plugin

add_library(example_mqtt_sender MODULE src/main.cpp)
target_link_libraries(example_mqtt_sender PRIVATE example_mqtt_sender_obj)

if(LINUX)
  target_link_options(
    example_mqtt_sender PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

install(TARGETS example_mqtt_sender DESTINATION plugins)

# Tests

if(BUILD_TESTING AND NOT WIN32)
  include(CTest)

  fetchContentAddCmake(Catch2 trompeloeil)

  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)

  add_executable(
    example_mqtt_sender_tests tests/test_mqtt_pub_sub.cpp
                              tests/test_mqtt_sender.cpp tests/test_util.hpp)

  target_include_directories(example_mqtt_sender_tests PRIVATE tests)
  target_link_libraries(
    example_mqtt_sender_tests
    PRIVATE example_mqtt_sender_obj Catch2::Catch2WithMain
            trompeloeil::trompeloeil)

  catch_discover_tests(example_mqtt_sender_tests)

  add_test(NAME example_mqtt_sender COMMAND example_mqtt_sender_tests)
endif()
