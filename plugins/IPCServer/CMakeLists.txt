cmake_minimum_required(VERSION 3.25)
project(IPCServer)

add_library(IPCServer MODULE main.cpp)
target_link_libraries(IPCServer PRIVATE nucleus-core)
add_custom_command(TARGET IPCServer
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:IPCServer>" "${PLUGIN_BINARY_DIR}"
        )
