cmake_minimum_required(VERSION 3.25)
project(gg_plugin_api)

add_library(gg_plugin_api INTERFACE
        ../scratch/provision.cpp)

target_include_directories(gg_plugin_api INTERFACE include)
