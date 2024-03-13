set(PLUGIN_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(device-sdk)

add_subdirectory(provision_plugin)
add_subdirectory(iot_broker)
add_subdirectory(ipc_server)
add_subdirectory(local_broker)
add_subdirectory(cli_server)
add_subdirectory(cloud_downloader)
add_subdirectory(tes_http_server_plugin)
