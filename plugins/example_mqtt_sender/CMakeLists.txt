cmake_minimum_required(VERSION 3.25)
project(example_mqtt_sender)

add_library(example_mqtt_sender MODULE main.cpp)
target_link_libraries(example_mqtt_sender PRIVATE gg_plugin_api)
