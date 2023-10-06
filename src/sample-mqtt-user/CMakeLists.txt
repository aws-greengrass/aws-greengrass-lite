cmake_minimum_required(VERSION 3.25)
project(sample-mqtt-user)

add_library(sample-mqtt-user SHARED sample-mqtt-user.cpp)
target_link_libraries(sample-mqtt-user PRIVATE nucleus-core)
