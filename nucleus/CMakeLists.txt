cmake_minimum_required(VERSION 3.25)
project(nucleus-core)

set(CMAKE_CXX_STANDARD 17)

add_compile_definitions(EXPORT_API)

if(UNIX)
    add_compile_definitions(USE_DLFCN)
elseif(WIN32)
    add_compile_definitions(USE_WINDLL)
endif()

set(FETCHCONTENT_QUIET FALSE)

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
if (NOT yaml-cpp_POPULATED)
    FetchContent_Populate(yaml-cpp)
    add_subdirectory(${yaml-cpp_SOURCE_DIR} ${yaml-cpp_BINARY_DIR} EXCLUDE_FROM_ALL)
endif ()

FetchContent_GetProperties(rapidjson)
if (NOT rapidjson_POPULATED)
    FetchContent_Populate(rapidjson)
endif ()

add_library(nucleus-core SHARED
        src/api/data.cpp
        src/api/error.cpp
        src/api/main.cpp
        src/api/plugins.cpp
        src/api/pubsub.cpp
        src/api/tasks.cpp
        src/config/config_manager.cpp src/config/config_manager.h
        src/config/json_helper.cpp src/config/json_helper.h
        src/config/publish_queue.cpp src/config/publish_queue.h
        src/config/transaction_log.cpp src/config/transaction_log.h
        src/config/watcher.h
        src/config/yaml_helper.cpp src/config/yaml_helper.h
        src/data/environment.cpp src/data/environment.h
        src/data/handle_table.cpp src/data/handle_table.h
        src/data/safe_handle.cpp src/data/safe_handle.h
        src/data/shared_buffer.cpp src/data/shared_buffer.h
        src/data/shared_list.cpp src/data/shared_list.h
        src/data/shared_struct.cpp src/data/shared_struct.h
        src/data/string_table.cpp src/data/string_table.h
        src/data/struct_model.cpp src/data/struct_model.h
        src/data/tracked_object.cpp src/data/tracked_object.h
        src/deployment/deployment_model.cpp src/deployment/deployment_model.h
        src/lifecycle/command_line.cpp src/lifecycle/command_line.h
        src/lifecycle/kernel.cpp src/lifecycle/kernel.h
        src/plugins/plugin_loader.cpp src/plugins/plugin_loader.h
        src/pubsub/local_topics.cpp src/pubsub/local_topics.h
        src/tasks/expire_time.h
        src/tasks/sub_task.h
        src/tasks/task.cpp src/tasks/task.h
        src/util/commitable_file.cpp src/util/commitable_file.h
        src/util/nucleus_paths.cpp src/util/nucleus_paths.h
        src/util/permissions.cpp src/util/permissions.h)

target_link_libraries(nucleus-core gg_plugin_api Threads::Threads ${CMAKE_DL_LIBS} yaml-cpp)
target_include_directories(nucleus-core
  PUBLIC include
  PRIVATE src ${rapidjson_SOURCE_DIR}/include)

add_executable(greengrass-lite stub/main.cpp)
target_link_libraries(greengrass-lite nucleus-core)
