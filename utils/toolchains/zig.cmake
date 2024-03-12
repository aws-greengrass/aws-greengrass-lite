set(CMAKE_SYSTEM_NAME "Linux")
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR ${ZIG_TARGET})
set(CMAKE_C_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-wrappers/zig-cc" -target
                     ${ZIG_TARGET})
set(CMAKE_CXX_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-wrappers/zig-c++" -target
                       ${ZIG_TARGET})
set(CMAKE_AR "${CMAKE_CURRENT_LIST_DIR}/zig-wrappers/zig-ar")
set(CMAKE_RANLIB "${CMAKE_CURRENT_LIST_DIR}/zig-wrappers/zig-ranlib")

set(CMAKE_C_COMPILER_AR "${CMAKE_AR}")
set(CMAKE_CXX_COMPILER_AR "${CMAKE_AR}")
set(CMAKE_C_COMPILER_RANLIB "${CMAKE_RANLIB}")
set(CMAKE_CXX_COMPILER_RANLIB "${CMAKE_RANLIB}")
