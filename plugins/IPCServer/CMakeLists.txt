cmake_minimum_required(VERSION 3.25)
project(IPCServer)

add_library(IPCServer MODULE main.cpp)
target_link_libraries(IPCServer PRIVATE nucleus-core)

install(TARGETS IPCServer DESTINATION plugins)
