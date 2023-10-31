cmake_minimum_required(VERSION 3.25)
project(nucleus-core)

set(CMAKE_CXX_STANDARD 17)

add_compile_definitions(EXPORT_API)

if (UNIX)
    add_compile_definitions(USE_DLFCN)
elseif (WIN32)
    add_compile_definitions(USE_WINDLL)
endif ()

add_compile_definitions(PLATFORM_SHLIB_SUFFIX=${CMAKE_SHARED_MODULE_SUFFIX})

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

#
# Build the set of objects used for both static library and shared object library
#
add_library(nucleus-obj OBJECT
        src/api/data.cpp
        src/api/error.cpp
        src/api/main.cpp
        src/api/plugins.cpp
        src/api/pubsub.cpp
        src/api/tasks.cpp
        src/config/config_manager.cpp src/config/config_manager.hpp
        src/config/json_helper.cpp src/config/json_helper.hpp
        src/config/publish_queue.cpp src/config/publish_queue.hpp
        src/config/transaction_log.cpp src/config/transaction_log.hpp
        src/config/watcher.hpp
        src/config/yaml_helper.cpp src/config/yaml_helper.hpp
        src/data/environment.cpp src/data/environment.hpp
        src/data/handle_table.cpp src/data/handle_table.hpp
        src/data/safe_handle.cpp src/data/safe_handle.hpp
        src/data/shared_buffer.cpp src/data/shared_buffer.hpp
        src/data/shared_list.cpp src/data/shared_list.hpp
        src/data/shared_struct.cpp src/data/shared_struct.hpp
        src/data/string_table.cpp src/data/string_table.hpp
        src/data/struct_model.cpp src/data/struct_model.hpp
        src/data/tracked_object.cpp src/data/tracked_object.hpp
        src/deployment/deployment_model.cpp src/deployment/deployment_model.hpp
        src/deployment/device_configuration.cpp src/deployment/device_configuration.hpp
        src/lifecycle/command_line.cpp src/lifecycle/command_line.hpp
        src/lifecycle/kernel.cpp src/lifecycle/kernel.hpp
        src/lifecycle/kernel_alternatives.cpp src/lifecycle/kernel_alternatives.hpp
        src/plugins/plugin_loader.cpp src/plugins/plugin_loader.hpp
        src/pubsub/local_topics.cpp src/pubsub/local_topics.hpp
        src/tasks/expire_time.hpp
        src/tasks/sub_task.hpp
        src/tasks/task.cpp src/tasks/task.hpp
        src/util/commitable_file.cpp src/util/commitable_file.hpp
        src/util/nucleus_paths.cpp src/util/nucleus_paths.hpp
        src/util/permissions.cpp src/util/permissions.hpp
)
target_link_libraries(nucleus-obj
        PUBLIC gg_plugin_api
        PRIVATE Threads::Threads ${CMAKE_DL_LIBS} yaml-cpp)
target_include_directories(nucleus-obj
        PUBLIC include
        PRIVATE src ${rapidjson_SOURCE_DIR}/include)
set_property(TARGET nucleus-obj PROPERTY POSITION_INDEPENDENT_CODE 1)

#
# Builds the shared-object "nucleus-core"
#
add_library(nucleus-core SHARED $<TARGET_OBJECTS:nucleus-obj>)
target_link_libraries(nucleus-core
        PUBLIC gg_plugin_api
        PRIVATE Threads::Threads ${CMAKE_DL_LIBS} yaml-cpp)
target_include_directories(nucleus-core
        PUBLIC include
        PRIVATE src ${rapidjson_SOURCE_DIR}/include)

#
# Build a static library for use by tests and also for single-binary use-cases
#
add_library(nucleus-lib STATIC $<TARGET_OBJECTS:nucleus-obj>)
target_link_libraries(nucleus-lib
        PUBLIC gg_plugin_api Threads::Threads ${CMAKE_DL_LIBS} yaml-cpp)

add_executable(greengrass-lite stub/main.cpp)
target_link_libraries(greengrass-lite nucleus-core)

#
# Build unit tests
#
add_executable(nucleus-tests
        tests/test_tools.hpp
        tests/test_ggroot.hpp
        tests/api/buffer_tests.cpp
        tests/api/list_tests.cpp
        tests/api/pubsub_tests.cpp
        tests/api/struct_tests.cpp
        tests/data/string_ord_tests.cpp
        tests/lifecycle/kernel_tests.cpp
        tests/pubsub/pubsub_tests.cpp
        tests/tasks/task_tests.cpp
)
target_link_libraries(nucleus-tests PRIVATE nucleus-lib Catch2::Catch2WithMain)
target_include_directories(nucleus-tests
        PRIVATE src tests ${rapidjson_SOURCE_DIR}/include)
include(Catch)
catch_discover_tests(nucleus-tests)


if (CMAKE_BUILD_TYPE STREQUAL Debug)
    if (UNIX)
        add_custom_target(coverage
                COMMENT "Running coverage for nucleus-tests"
                COMMAND lcov -d . --zerocounters
                COMMAND lcov -d . --capture -o coverage.info
                COMMAND lcov -r coverage.info '/usr/include/*' -o filtered.info
                COMMAND genhtml -o coverage filtered.info
                COMMAND genhtml -o coverage filtered.info --legend
                COMMAND rm -rf coverage.info filtered.info
                DEPENDS nucleus-tests
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        )
    endif ()
endif ()
