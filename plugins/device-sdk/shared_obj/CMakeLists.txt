enable_warnings()

add_compile_definitions(EXPORT_DEVICESDK_API)

add_library(device-sdk SHARED include/shared_device_sdk.hpp src/main.cpp)

if(APPLE)
  target_link_libraries(
    device-sdk PRIVATE -Wl,-all_load aws-crt-cpp IotIdentity-cpp aws-c-http
                       aws-c-event-stream -Wl,-noall_load)
else()
  target_link_libraries(
    device-sdk PRIVATE -Wl,--whole-archive aws-crt-cpp IotIdentity-cpp
                       aws-c-http aws-c-event-stream -Wl,--no-whole-archive)
endif()

target_include_directories(device-sdk PUBLIC include)

target_include_directories(
  device-sdk SYSTEM
  PUBLIC $<TARGET_PROPERTY:aws-crt-cpp,INTERFACE_INCLUDE_DIRECTORIES>
         $<TARGET_PROPERTY:IotIdentity-cpp,INTERFACE_INCLUDE_DIRECTORIES>
         $<TARGET_PROPERTY:aws-c-http,INTERFACE_INCLUDE_DIRECTORIES>
         $<TARGET_PROPERTY:aws-c-event-stream,INTERFACE_INCLUDE_DIRECTORIES>)

configureRPATH(device-sdk)

install(TARGETS device-sdk RUNTIME_DEPENDENCY_SET device-sdk-deps)

if(INSTALL_RUNTIME_DEPENDENCIES)
  install(RUNTIME_DEPENDENCY_SET device-sdk-deps)
endif()
