include_guard(GLOBAL)
cmake_minimum_required(VERSION 3.25)
project(nucleus-core)

include(../common.cmake)

add_subdirectory(../plugin_api plugin_api EXCLUDE_FROM_ALL)
add_subdirectory(../gg_pal/ gg_pal EXCLUDE_FROM_ALL)

find_package(Threads REQUIRED)

fetchContentAddCmake(yaml-cpp)
fetchContentAdd(rapidjson)

add_compile_definitions(EXPORT_NUCLEUS_API)

if(UNIX)
  add_compile_definitions(USE_DLFCN)
elseif(WIN32)
  add_compile_definitions(USE_WINDLL)
endif()

add_compile_definitions(PLATFORM_SHLIB_SUFFIX=${CMAKE_SHARED_MODULE_SUFFIX})

enable_warnings()

# Object library shared between shared lib and tests

add_library(
  nucleus-obj OBJECT
  include/nucleus_core.h
  src/api/api_error_trap.hpp
  src/api/data.cpp
  src/api/error.cpp
  src/api/logging.cpp
  src/api/main.cpp
  src/api/plugins.cpp
  src/api/pubsub.cpp
  src/api/tasks.cpp
  src/api/channel.cpp
  src/channel/channel.cpp
  src/channel/channel.hpp
  src/config/config_manager.cpp
  src/config/config_manager.hpp
  src/config/config_nodes.hpp
  src/config/config_timestamp.hpp
  src/config/publish_queue.cpp
  src/config/publish_queue.hpp
  src/config/tlog_json.cpp
  src/config/tlog_json.hpp
  src/config/transaction_log.cpp
  src/config/transaction_log.hpp
  src/config/watcher.hpp
  src/config/update_behavior_tree.hpp
  src/config/yaml_config.cpp
  src/config/yaml_config.hpp
  src/conv/json_conv.cpp
  src/conv/json_conv.hpp
  src/conv/yaml_conv.cpp
  src/conv/yaml_conv.hpp
  src/data/data_util.hpp
  src/data/handle_table.cpp
  src/data/handle_table.hpp
  src/data/safe_handle.hpp
  src/data/generic_serializer.cpp
  src/data/generic_serializer.hpp
  src/data/serializable.cpp
  src/data/serializable.hpp
  src/data/shared_buffer.cpp
  src/data/shared_buffer.hpp
  src/data/shared_list.cpp
  src/data/shared_list.hpp
  src/data/shared_struct.cpp
  src/data/shared_struct.hpp
  src/data/shared_queue.hpp
  src/data/string_table.cpp
  src/data/string_table.hpp
  src/data/struct_model.cpp
  src/data/struct_model.hpp
  src/data/symbol_value_map.hpp
  src/data/tracked_object.cpp
  src/data/tracked_object.hpp
  src/data/value_type.hpp
  src/deployment/config_watchers.hpp
  src/deployment/deployment_manager.cpp
  src/deployment/deployment_manager.hpp
  src/deployment/deployment_model.cpp
  src/deployment/deployment_model.hpp
  src/deployment/device_configuration.cpp
  src/deployment/device_configuration.cpp
  src/deployment/model/dependency_order.hpp
  src/deployment/model/deployment_queue.cpp
  src/deployment/model/deployment_queue.hpp
  src/deployment/task/default_deployment_task.cpp
  src/deployment/task/default_deployment_task.hpp
  src/deployment/model/linked_map.hpp
  src/deployment/recipe_model.hpp
  src/deployment/recipe_loader.hpp
  src/deployment/recipe_loader.cpp
  src/errors/api_errors.hpp
  src/errors/error_base.cpp
  src/errors/error_base.hpp
  src/errors/errors.hpp
  src/lifecycle/command_line.cpp
  src/lifecycle/command_line.hpp
  src/lifecycle/command_line_arguments.hpp
  src/lifecycle/error_rate.hpp
  src/lifecycle/kernel.cpp
  src/lifecycle/kernel.hpp
  src/lifecycle/lifecycle_fsm.cpp
  src/lifecycle/lifecycle_fsm.hpp
  src/lifecycle/lifecycle_manager.cpp
  src/lifecycle/lifecycle_manager.hpp
  src/lifecycle/scripting.cpp
  src/lifecycle/scripting.hpp
  src/lifecycle/kernel_alternatives.cpp
  src/lifecycle/kernel_alternatives.hpp
  src/lifecycle/sys_properties.cpp
  src/lifecycle/sys_properties.hpp
  src/logging/log_manager.cpp
  src/logging/log_manager.hpp
  src/logging/log_queue.cpp
  src/logging/log_queue.hpp
  src/plugins/plugin_loader.cpp
  src/plugins/plugin_loader.hpp
  src/pubsub/local_topics.cpp
  src/pubsub/local_topics.hpp
  src/pubsub/promise.cpp
  src/pubsub/promise.hpp
  src/scope/context.cpp
  src/scope/context.hpp
  src/scope/context_full.hpp
  src/scope/context_glob.hpp
  src/scope/fixed_pointer.hpp
  src/scope/mapper.hpp
  src/tasks/expire_time.hpp
  src/tasks/task.hpp
  src/tasks/task_callbacks.cpp
  src/tasks/task_callbacks.hpp
  src/tasks/task_manager.cpp
  src/tasks/task_manager.hpp
  src/tasks/task_threads.cpp
  src/tasks/task_threads.hpp
  src/util/commitable_file.cpp
  src/util/commitable_file.hpp
  src/util/nucleus_paths.cpp
  src/util/nucleus_paths.hpp
  src/util/permissions.cpp
  src/util/permissions.hpp
  src/deployment/task/task_handlers/prepare_artifacts_handler.hpp
  src/deployment/task/task_handlers/resolve_config_handler.hpp
  src/deployment/task/task_handlers/merge_config_handler.hpp
  src/deployment/task/task_handlers/track_service_status_handler.hpp
  src/deployment/task/task_handlers/task_handler.hpp
  src/deployment/task/task_handlers/validate_deployment_handler.hpp
  src/deployment/task/task_handlers/validate_deployment_handler.cpp)

target_link_libraries(
  nucleus-obj PUBLIC gg_plugin_api_min gg_pal Threads::Threads ${CMAKE_DL_LIBS}
                     yaml-cpp)
target_include_directories(nucleus-obj PUBLIC include src)
target_include_directories(nucleus-obj SYSTEM
                           PUBLIC ${rapidjson_SOURCE_DIR}/include)

# Shared library

add_library(nucleus-core SHARED)
target_link_libraries(
  nucleus-core
  PUBLIC gg_plugin_api_min
  PRIVATE nucleus-obj)

target_include_directories(nucleus-core PUBLIC include)

configureRPATH(nucleus-core)

# Executable

add_executable(greengrass-lite stub/main.c)
target_link_libraries(greengrass-lite nucleus-core)

configureRPATH(greengrass-lite)

install(TARGETS greengrass-lite nucleus-core RUNTIME_DEPENDENCY_SET
                nucleus-deps)

if(INSTALL_RUNTIME_DEPENDENCIES)
  install(RUNTIME_DEPENDENCY_SET nucleus-deps)
endif()

# Tests

if(BUILD_TESTING)
  include(CTest)

  fetchContentAddCmake(Catch2 trompeloeil)

  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)

  add_executable(
    nucleus-tests
    tests/api/archive_tests.cpp
    tests/api/buffer_tests.cpp
    tests/api/callback_tests.cpp
    tests/api/enum_tests.cpp
    tests/api/json_tests.cpp
    tests/api/list_tests.cpp
    tests/api/logging_tests.cpp
    tests/api/lookup_table_tests.cpp
    tests/api/module_scope_tests.cpp
    tests/api/pubsub_tests.cpp
    tests/api/struct_tests.cpp
    tests/api/task_tests.cpp
    tests/data/handle_tests.cpp
    tests/data/obfuscator_test.cpp
    tests/data/struct_tests.cpp
    tests/data/symbol_tests.cpp
    tests/deployment/deploy_tests.cpp
    tests/deployment/model/dependency_order_test.cpp
    tests/deployment/model/deployment_queue_test.cpp
    tests/deployment/model/linked_map_test.cpp
    tests/deployment/recipe_tests.cpp
    tests/deployment/tasks/deployment_task_handler_test.cpp
    tests/errors/errors_test.cpp
    tests/lifecycle/kernel_tests.cpp
    tests/lifecycle/lifecycle_tests.cpp
    tests/pubsub/basic_pubsub_tests.cpp
    tests/scope/context_tests.cpp
    tests/tasks/expire_time_tests.cpp
    tests/tasks/task_tests.cpp
    tests/test_ggroot.hpp
    tests/test_tools.hpp)

  target_link_libraries(
    nucleus-tests PRIVATE nucleus-obj Catch2::Catch2WithMain
                          trompeloeil::trompeloeil)
  target_include_directories(nucleus-tests PRIVATE tests)

  catch_discover_tests(nucleus-tests)

  file(COPY tests/samples DESTINATION ${CMAKE_BINARY_DIR})

  if(UNIX)
    target_compile_options(nucleus-tests PRIVATE --coverage)
    target_link_options(nucleus-tests PUBLIC --coverage)
    add_custom_command(
      TARGET nucleus-tests
      COMMENT "Delete existing coverage info"
      PRE_BUILD
      COMMAND find ${CMAKE_BINARY_DIR} -type f -name '*.gcda' -exec rm {} +)
    add_custom_target(
      coverage
      COMMENT "Running coverage for nucleus-tests"
      COMMAND lcov -d . --zerocounters
      COMMAND lcov -d . --capture -o coverage.info
      COMMAND lcov -r coverage.info '/usr/include/*' -o filtered.info
      COMMAND genhtml -o coverage filtered.info
      COMMAND genhtml -o coverage filtered.info --legend
      COMMAND rm -rf coverage.info filtered.info
      DEPENDS nucleus-tests
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  endif()

  add_test(NAME nucleus COMMAND nucleus-tests)
endif()
