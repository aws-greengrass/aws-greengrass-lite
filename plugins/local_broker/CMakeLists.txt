cmake_minimum_required(VERSION 3.22)
project(local_broker)

add_library(local_broker MODULE src/main.cpp)
target_link_libraries(local_broker PRIVATE gg_plugin_api shared_resources)

if(UNIX AND NOT APPLE)
  target_link_options(
    local_broker PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

install(TARGETS local_broker DESTINATION plugins)
