cmake_minimum_required(VERSION 3.22)
project(tes_http_server_plugin)

add_library(tes_http_server_plugin MODULE main.cpp src/tes_http_server.hpp src/tes_http_server.cpp)
target_link_libraries(tes_http_server_plugin PRIVATE nucleus-core aws-c-http aws-crt-cpp)
target_include_directories(
        tes_http_server_plugin
        PUBLIC include
        PRIVATE src ${rapidjson_SOURCE_DIR}/include)
install(TARGETS tes_http_server_plugin DESTINATION plugins)
