function(quantapdf_configure_shared_exports target manifest)
  if(NOT BUILD_SHARED_LIBS)
    return()
  endif()

  set_target_properties(${target} PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

  if(APPLE)
    file(STRINGS "${manifest}" _quantapdf_symbols)
    set(_quantapdf_export_list
      "${CMAKE_CURRENT_BINARY_DIR}/${target}-exports.txt")
    file(WRITE "${_quantapdf_export_list}" "")
    foreach(_quantapdf_symbol IN LISTS _quantapdf_symbols)
      file(APPEND "${_quantapdf_export_list}"
        "_${_quantapdf_symbol}\n")
    endforeach()
    target_link_options(${target} PRIVATE
      "LINKER:-exported_symbols_list,${_quantapdf_export_list}")
  elseif(UNIX)
    file(STRINGS "${manifest}" _quantapdf_symbols)
    set(_quantapdf_version_script
      "${CMAKE_CURRENT_BINARY_DIR}/${target}-exports.map")
    file(WRITE "${_quantapdf_version_script}"
      "{\n  global:\n")
    foreach(_quantapdf_symbol IN LISTS _quantapdf_symbols)
      file(APPEND "${_quantapdf_version_script}"
        "    ${_quantapdf_symbol};\n")
    endforeach()
    file(APPEND "${_quantapdf_version_script}"
      "  local:\n    *;\n};\n")
    target_link_options(${target} PRIVATE
      "LINKER:--version-script=${_quantapdf_version_script}"
      "LINKER:--exclude-libs,ALL"
      "LINKER:-Bsymbolic-functions")
  endif()
endfunction()
