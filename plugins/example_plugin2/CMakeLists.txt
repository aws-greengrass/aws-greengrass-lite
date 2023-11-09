cmake_minimum_required(VERSION 3.25)
project(example_plugin2)

add_library(example_plugin2 MODULE main.cpp)
target_link_libraries(example_plugin2 PRIVATE nucleus-core)

install(TARGETS example_plugin2 DESTINATION plugins)
