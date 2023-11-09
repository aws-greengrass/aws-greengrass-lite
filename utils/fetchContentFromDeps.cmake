set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)

file(READ "${CMAKE_CURRENT_LIST_DIR}/../dependencies.json" PROJECT_DEPS_JSON)

function(fetchContentFromDeps)
    foreach(name IN LISTS ARGN)
        string(JSON url GET "${PROJECT_DEPS_JSON}" "${name}" url)
        string(JSON rev GET "${PROJECT_DEPS_JSON}" "${name}" rev)
        string(TOLOWER "${name}" lower_name)

        FetchContent_Declare("${name}"
            GIT_REPOSITORY "${url}"
            GIT_TAG "${rev}"
            GIT_SHALLOW TRUE)
    endforeach()
endfunction()

