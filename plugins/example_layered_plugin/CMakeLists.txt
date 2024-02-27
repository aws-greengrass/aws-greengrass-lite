cmake_minimum_required(VERSION 3.22)
project(example_layered_plugin)

add_library(example_layered_plugin MODULE src/main.cpp)
target_link_libraries(example_layered_plugin PRIVATE gg_plugin_api)

# Uncomment this to install this sample plugin
#install(TARGETS example_layered_plugin DESTINATION plugins)
