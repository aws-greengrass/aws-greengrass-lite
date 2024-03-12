include_guard()

# Set version script if able
function(set_version_script target script)
  check_linker_flag(
    CXX "LINKER:--version-script=${CMAKE_CURRENT_LIST_DIR}/version.script"
    linker_has_version_script)
  if(linker_has_version_script)
    target_link_options(${target} PRIVATE "LINKER:--version-script=${script}")
  endif()
endfunction()
