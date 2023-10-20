cmake_minimum_required(VERSION 3.25)
project(example_mqtt_sender)

add_library(example_mqtt_sender MODULE main.cpp)
target_link_libraries(example_mqtt_sender PRIVATE nucleus-core)
add_custom_command(TARGET example_mqtt_sender
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:example_mqtt_sender>" "${PLUGIN_BINARY_DIR}"
        )
