cmake_minimum_required(VERSION 3.25)
project(example_plugin1)

add_library(example_plugin1 MODULE main.cpp)
target_link_libraries(example_plugin1 PRIVATE nucleus-core)

install(TARGETS example_plugin1 DESTINATION plugins)
