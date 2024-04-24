include_guard(GLOBAL)
cmake_minimum_required(VERSION 3.25)
project(shared_resources)

add_library(shared_resources INTERFACE device_sdk/device_sdk_support.hpp
            mqtt/topic_filter.hpp mqtt/topic_level_iterator.hpp)

target_include_directories(shared_resources INTERFACE .)

add_library(shared_test_resources INTERFACE test/temp_dir.hpp
            test/plugin_lifecycle.hpp)

target_link_libraries(shared_test_resources INTERFACE shared_resources
                                                      gg_plugin_api_min)
