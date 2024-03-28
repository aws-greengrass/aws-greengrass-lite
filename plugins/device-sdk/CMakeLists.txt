include_guard(GLOBAL)
cmake_minimum_required(VERSION 3.25)
project(device-sdk)

include(../../common.cmake)

# split into subdirectories to isolate directory-level properties

add_subdirectory(sdk)

add_subdirectory(shared_obj)
