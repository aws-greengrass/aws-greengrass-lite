cmake_minimum_required(VERSION 3.25)
project(example_plugin1)

add_library(example_plugin1 MODULE main.cpp)
target_link_libraries(example_plugin1 PRIVATE nucleus-core)
add_custom_command(TARGET example_plugin1
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:example_plugin1>" "${PLUGIN_BINARY_DIR}"
        )
