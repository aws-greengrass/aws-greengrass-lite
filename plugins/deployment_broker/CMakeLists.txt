cmake_minimum_required(VERSION 3.22)
project(deployment_broker)

include(FetchContent)
include(../../utils/fetchContentFromDeps.cmake)

fetchContentFromDeps(yaml-cpp)

FetchContent_GetProperties(yaml-cpp)

if(NOT yaml-cpp_POPULATED)
  FetchContent_Populate(yaml-cpp)
  add_subdirectory(${yaml-cpp_SOURCE_DIR} ${yaml-cpp_BINARY_DIR}
                   EXCLUDE_FROM_ALL)
endif()

add_library(deployment_broker MODULE main.cpp src/deployment_broker.cpp
                                     src/deployment_broker.hpp)
target_link_libraries(deployment_broker PRIVATE nucleus-core yaml-cpp)
target_include_directories(deployment_broker PRIVATE src)
if(UNIX AND NOT APPLE)
  target_link_options(
    deployment_broker PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

install(TARGETS deployment_broker DESTINATION plugins)
