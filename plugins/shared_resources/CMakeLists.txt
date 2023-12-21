cmake_minimum_required(VERSION 3.22)
project(shared_resources)

add_library(shared_resources INTERFACE)
target_include_directories(shared_resources INTERFACE .)
