cmake_minimum_required(VERSION 3.22)
project(example_mqtt_sender)

add_library(example_mqtt_sender_obj OBJECT src/example_mqtt_sender.cpp
                                           src/example_mqtt_sender.hpp)
target_link_libraries(example_mqtt_sender_obj PRIVATE gg_plugin_api)
target_include_directories(example_mqtt_sender_obj PUBLIC src)
set_property(TARGET example_mqtt_sender_obj PROPERTY POSITION_INDEPENDENT_CODE
                                                     1)

add_library(example_mqtt_sender MODULE src/main.cpp)
target_link_libraries(example_mqtt_sender PRIVATE example_mqtt_sender_obj
                                                  gg_plugin_api)
target_include_directories(example_mqtt_sender PRIVATE src)

if(UNIX AND NOT APPLE)
  target_link_options(
    example_mqtt_sender PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

# Uncomment this to install this sample plugin
#install(TARGETS example_mqtt_sender DESTINATION plugins)

add_library(example_mqtt_sender_lib STATIC)
target_link_libraries(example_mqtt_sender_lib PUBLIC example_mqtt_sender_obj
                                                     nucleus-lib)
target_include_directories(example_mqtt_sender_lib PUBLIC src)

if(UNIX)
  add_executable(
    example_mqtt_sender_tests tests/test_mqtt_pub_sub.cpp
                              tests/test_mqtt_sender.cpp tests/test_util.hpp)
  target_link_libraries(
    example_mqtt_sender_tests
    PUBLIC example_mqtt_sender_lib Catch2::Catch2WithMain
           trompeloeil::trompeloeil)
  target_include_directories(example_mqtt_sender_tests PRIVATE tests)
  catch_discover_tests(example_mqtt_sender_tests)
endif()
