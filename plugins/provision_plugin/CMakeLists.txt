cmake_minimum_required(VERSION 3.25)
project(provision_plugin)

include(../../utils/fetchContentFromDeps.cmake)
fetchContentFromDeps(device-sdk-cpp)

FetchContent_GetProperties(device-sdk-cpp)
if (NOT device-sdk-cpp_POPULATED)
    FetchContent_Populate(device-sdk-cpp)
    add_subdirectory(${device-sdk-cpp_SOURCE_DIR} ${device-sdk-cpp_BINARY_DIR} EXCLUDE_FROM_ALL)
endif ()

add_library(provision_plugin MODULE main.cpp
        src/device_provisioning.cpp include/device_provisioning.hpp
)
target_include_directories(provision_plugin PRIVATE include)
target_link_libraries(provision_plugin PRIVATE nucleus-core aws-crt-cpp IotIdentity-cpp)
install(TARGETS provision_plugin DESTINATION plugins)
