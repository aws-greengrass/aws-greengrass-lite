include_guard()

# Set RPATH to be origin relative
function(configureRPATH)
  foreach(name IN LISTS ARGN)
    set_target_properties(${name} PROPERTIES BUILD_RPATH_USE_ORIGIN ON)

    if(APPLE)
      set_target_properties(${name} PROPERTIES INSTALL_RPATH
                                               "@loader_path/../lib")
    elseif(UNIX)
      set_target_properties(${name} PROPERTIES INSTALL_RPATH "\$ORIGIN/../lib")
    endif()
  endforeach()
endfunction()
