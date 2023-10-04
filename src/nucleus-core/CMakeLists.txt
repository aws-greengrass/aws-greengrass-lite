cmake_minimum_required(VERSION 3.25)
project(nucleus-core)

set(CMAKE_CXX_STANDARD 17)

add_compile_definitions(EXPORT_API)

include(FetchContent)

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG f7320141120f720aecc4c32be25586e7da9eb978)

FetchContent_Declare(
  rapidjson
  GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
  GIT_TAG f54b0e47a08782a6131cc3d60f94d038fa6e0a51
  GIT_SHALLOW TRUE)

FetchContent_GetProperties(yaml-cpp)
if(NOT yaml-cpp_POPULATED)
  FetchContent_Populate(yaml-cpp)
  add_subdirectory(${yaml-cpp_SOURCE_DIR} ${yaml-cpp_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

FetchContent_GetProperties(rapidjson)
if(NOT rapidjson_POPULATED)
  FetchContent_Populate(rapidjson)
endif()

add_library(nucleus-core SHARED
        api/data.cpp
        api/error.cpp
        api/main.cpp
        api/plugins.cpp
        api/pubsub.cpp
        api/tasks.cpp
        config/config_manager.cpp config/config_manager.h
        config/json_helper.cpp config/json_helper.h
        config/transaction_log.cpp config/transaction_log.h
        config/watcher.h
        config/yaml_helper.cpp config/yaml_helper.h
        data/environment.cpp data/environment.h
        data/handle_table.cpp data/handle_table.h
        data/safe_handle.cpp data/safe_handle.h
        data/shared_buffer.cpp data/shared_buffer.h
        data/shared_list.cpp data/shared_list.h
        data/shared_struct.cpp data/shared_struct.h
        data/string_table.cpp data/string_table.h
        data/struct_model.cpp data/struct_model.h
        data/tracked_object.cpp data/tracked_object.h
        deployment/deployment_model.cpp deployment/deployment_model.h
        lifecycle/command_line.cpp lifecycle/command_line.h
        lifecycle/kernel.cpp lifecycle/kernel.h
        plugins/plugin_loader.cpp plugins/plugin_loader.h
        pubsub/local_topics.cpp pubsub/local_topics.h
        tasks/expire_time.h
        tasks/sub_task.h
        tasks/task.cpp tasks/task.h
        util/nucleus_paths.cpp util/nucleus_paths.h
        util/permissions.cpp util/permissions.h
        config/json_helper.cpp)
target_link_libraries(nucleus-core Threads::Threads ${CMAKE_DL_LIBS} yaml-cpp)
target_include_directories(nucleus-core PRIVATE . ${rapidjson_SOURCE_DIR}/include)

add_custom_command(TARGET nucleus-core
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:nucleus-core>" "${TOP_BINARY_DIR}"
        )
