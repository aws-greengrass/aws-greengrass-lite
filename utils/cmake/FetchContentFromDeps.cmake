include_guard()

set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)

file(READ "${CMAKE_CURRENT_LIST_DIR}/../../dependencies.json" PROJECT_DEPS_JSON)

# Function to get FetchContent declarations from dependencies.json
function(fetchContentFromDeps)
  foreach(name IN LISTS ARGN)
    string(JSON url GET "${PROJECT_DEPS_JSON}" "${name}" url)
    string(JSON rev GET "${PROJECT_DEPS_JSON}" "${name}" rev)

    FetchContent_Declare(
      "${name}"
      GIT_REPOSITORY "${url}"
      GIT_TAG "${rev}")
  endforeach()
endfunction()

# Function to get FetchContent dependencies without calling add_subdirectory
function(fetchContentAdd)
  foreach(name IN LISTS ARGN)
    fetchContentFromDeps(${name})

    string(TOLOWER "${name}" lower_name)

    if(NOT ${lower_name}_POPULATED)
      FetchContent_Populate(${name})
    endif()

    set(${lower_name}_POPULATED
        ${${lower_name}_POPULATED}
        PARENT_SCOPE)
    set(${lower_name}_SOURCE_DIR
        ${${lower_name}_SOURCE_DIR}
        PARENT_SCOPE)
    set(${lower_name}_BINARY_DIR
        ${${lower_name}_BINARY_DIR}
        PARENT_SCOPE)
  endforeach()
endfunction()

# Function to get FetchContent dependencies with calling add_subdirectory
function(fetchContentAddCmake)
  foreach(name IN LISTS ARGN)
    fetchContentFromDeps(${name})

    string(TOLOWER "${name}" lower_name)

    FetchContent_GetProperties(${name})
    if(NOT ${lower_name}_POPULATED)
      FetchContent_Populate(${name})

      set(BUILD_TESTING OFF)

      add_subdirectory(${${lower_name}_SOURCE_DIR} ${${lower_name}_BINARY_DIR}
                       EXCLUDE_FROM_ALL SYSTEM)
    endif()

    set(${lower_name}_POPULATED
        ${${lower_name}_POPULATED}
        PARENT_SCOPE)
    set(${lower_name}_SOURCE_DIR
        ${${lower_name}_SOURCE_DIR}
        PARENT_SCOPE)
    set(${lower_name}_BINARY_DIR
        ${${lower_name}_BINARY_DIR}
        PARENT_SCOPE)
  endforeach()
endfunction()
