cmake_minimum_required(VERSION 3.25)
project(example_layered_plugin)

add_library(example_layered_plugin MODULE main.cpp)
target_link_libraries(example_layered_plugin PRIVATE nucleus-core)
add_custom_command(TARGET example_layered_plugin
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:example_layered_plugin>" "${PLUGIN_BINARY_DIR}"
        )
