cmake_minimum_required(VERSION 3.22)
project(sample-component)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COMPONENT_NAME com.example.SampleComponent)
set(COMPONENT_VERSION 1.0.0)

set(FETCHCONTENT_QUIET FALSE)

include(../utils/fetchContentFromDeps.cmake)

fetchContentAddCmake(device-sdk-cpp)

add_executable(sample-component main.cpp)

install(TARGETS sample-component
        DESTINATION artifacts/${COMPONENT_NAME}/${COMPONENT_VERSION})
install(FILES recipes/${COMPONENT_NAME}-${COMPONENT_VERSION}.yaml
        DESTINATION recipes)

target_link_libraries(sample-component PRIVATE GreengrassIpc-cpp)
