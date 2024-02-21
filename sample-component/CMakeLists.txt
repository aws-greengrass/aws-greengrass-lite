cmake_minimum_required(VERSION 3.22)
project(sample-component)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COMPONENT_NAME com.example.SampleComponent)
set(COMPONENT_VERSION 1.0.0)

set(FETCHCONTENT_QUIET FALSE)
include(FetchContent)
include(../utils/fetchContentFromDeps.cmake)

fetchcontentfromdeps(device-sdk-cpp)

FetchContent_GetProperties(device-sdk-cpp)
if(NOT device-sdk-cpp_POPULATED)
  FetchContent_Populate(device-sdk-cpp)
  add_subdirectory(${device-sdk-cpp_SOURCE_DIR} ${device-sdk-cpp_BINARY_DIR}
                   EXCLUDE_FROM_ALL)
endif()

add_executable(sample-component main.cpp)

install(TARGETS sample-component
        DESTINATION artifacts/${COMPONENT_NAME}/${COMPONENT_VERSION})
install(FILES recipes/${COMPONENT_NAME}-${COMPONENT_VERSION}.yaml
        DESTINATION recipes)

target_link_libraries(sample-component PRIVATE GreengrassIpc-cpp)
