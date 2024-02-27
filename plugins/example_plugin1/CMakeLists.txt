cmake_minimum_required(VERSION 3.22)
project(example_plugin1)

add_library(example_plugin1 MODULE src/main.cpp)
target_link_libraries(example_plugin1 PRIVATE gg_plugin_api)

# Uncomment this to install this sample plugin
#install(TARGETS example_plugin1 DESTINATION plugins)
