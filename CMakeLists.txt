cmake_minimum_required(VERSION 3.25)
project(greengrass-lite-all)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
# set(CMAKE_CXX_FLAGS_RELEASE "-Os") #-- for size
# set(CMAKE_CXX_FLAGS_RELEASE "-O3") #-- for speed

# Uncomment to enable santizers - thread & address cannot be combined
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address")
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=undefined")
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=null")
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=thread")

find_package(Threads REQUIRED)

if(UNIX)
    add_compile_definitions(USE_DLFCN)
    add_compile_options(-fvisibility=hidden)
elseif(WIN32)
    add_compile_definitions(USE_WINDLL)
  endif()

add_compile_definitions(PLATFORM_SHLIB_SUFFIX=${CMAKE_SHARED_LIBRARY_SUFFIX})

include_directories(include)

add_subdirectory(src)
