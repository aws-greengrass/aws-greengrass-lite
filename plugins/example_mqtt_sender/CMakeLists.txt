cmake_minimum_required(VERSION 3.25)
project(example_mqtt_sender)

add_library(example_mqtt_sender_obj OBJECT src/example_mqtt_sender.cpp
                                           include/example_mqtt_sender.hpp)
target_link_libraries(example_mqtt_sender_obj PUBLIC nucleus-core)
target_include_directories(example_mqtt_sender_obj PUBLIC include)
set_property(TARGET example_mqtt_sender_obj PROPERTY POSITION_INDEPENDENT_CODE
                                                     1)

add_library(example_mqtt_sender MODULE $<TARGET_OBJECTS:example_mqtt_sender_obj>
                                       main.cpp)
target_link_libraries(example_mqtt_sender PRIVATE nucleus-core)
target_include_directories(example_mqtt_sender PRIVATE include)

add_library(example_mqtt_sender_lib STATIC
            $<TARGET_OBJECTS:example_mqtt_sender_obj>)
target_link_libraries(example_mqtt_sender_lib PUBLIC nucleus-lib)
target_include_directories(example_mqtt_sender_lib PUBLIC include)

install(TARGETS example_mqtt_sender DESTINATION plugins)

add_executable(
  example_mqtt_sender_tests tests/test_mqtt_pub_sub.cpp
                            tests/test_mqtt_sender.cpp tests/test_util.hpp)
target_include_directories(example_mqtt_sender_tests PRIVATE tests)

target_link_libraries(
  example_mqtt_sender_tests PRIVATE example_mqtt_sender_lib nucleus-lib
                                    Catch2::Catch2WithMain)
catch_discover_tests(example_mqtt_sender_tests)
