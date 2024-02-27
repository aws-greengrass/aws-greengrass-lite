cmake_minimum_required(VERSION 3.22)
project(local_broker)

add_library(local_broker MODULE src/main.cpp)
target_link_libraries(local_broker PRIVATE nucleus-core shared_resources)

install(TARGETS local_broker DESTINATION plugins)
