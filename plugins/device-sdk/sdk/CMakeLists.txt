set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
cmake_policy(SET CMP0077 NEW)

find_package(OpenSSL)
if(OpenSSL_FOUND)
  set(USE_OPENSSL ON)
endif()

if(UNIX)
  add_compile_options(-fvisibility=default)
endif()

add_compile_options(-w)

fetchContentAddCmake(aws-crt-cpp)

set(BUILD_DEPS OFF)
set(USE_EXTERNAL_DEPS_SOURCES ON)

fetchContentAddCmake(aws-c-iot)

fetchContentAddCmake(aws-iot-device-sdk-cpp-v2)
