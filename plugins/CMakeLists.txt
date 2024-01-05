set(PLUGIN_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(shared_resources)

add_subdirectory(example_layered_plugin)
add_subdirectory(example_mqtt_sender)
add_subdirectory(example_plugin1)
add_subdirectory(provision_plugin)
add_subdirectory(iot_broker)
add_subdirectory(ipc_server)
add_subdirectory(local_broker)
add_subdirectory(cli_server)
