cmake_minimum_required(VERSION 3.22)
project(provision_plugin)

include(../../utils/fetchContentFromDeps.cmake)

fetchContentAddCmake(device-sdk-cpp)

add_library(provision_plugin MODULE include/provision_plugin.hpp main.cpp
                                    src/provision_plugin.cpp)
target_include_directories(provision_plugin PRIVATE include)
target_link_libraries(provision_plugin PRIVATE nucleus-core aws-crt-cpp
                                               IotIdentity-cpp)
if(UNIX AND NOT APPLE)
  target_link_options(
    provision_plugin PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

install(TARGETS provision_plugin DESTINATION plugins)
