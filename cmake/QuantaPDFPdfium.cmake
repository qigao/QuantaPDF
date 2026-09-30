include_guard(GLOBAL)

function(quantapdf_import_pdfium)
  if(TARGET QuantaPDF::PDFium)
    return()
  endif()

  set(_release_api
      "https://api.github.com/repos/bblanchon/pdfium-binaries/releases/latest")
  set(_target_processor "${CMAKE_SYSTEM_PROCESSOR}")

  if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES _architecture_count)
    if(NOT _architecture_count EQUAL 1)
      message(FATAL_ERROR
        "QuantaPDF requires a single-architecture macOS build for PDFium artifacts")
    endif()
    list(GET CMAKE_OSX_ARCHITECTURES 0 _target_processor)
  elseif(WIN32 AND CMAKE_GENERATOR_PLATFORM)
    set(_target_processor "${CMAKE_GENERATOR_PLATFORM}")
  endif()

  if(WIN32
     AND CMAKE_SIZEOF_VOID_P EQUAL 8
     AND _target_processor MATCHES "^(x64|x86_64|amd64|AMD64)$")
    set(_platform "win-x64")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux"
         AND _target_processor MATCHES "^(x86_64|amd64|AMD64)$")
    set(_platform "linux-x64")
  elseif(APPLE AND _target_processor MATCHES "^(x86_64|amd64|AMD64)$")
    set(_platform "mac-x64")
  elseif(APPLE AND _target_processor MATCHES "^(arm64|aarch64)$")
    set(_platform "mac-arm64")
  else()
    message(FATAL_ERROR
      "Latest PDFium release has no supported artifact mapping for "
      "${CMAKE_SYSTEM_NAME}/${_target_processor}")
  endif()

  set(_download_directory "${CMAKE_BINARY_DIR}/_deps/downloads")
  file(MAKE_DIRECTORY "${_download_directory}")
  set(_release_metadata "${_download_directory}/pdfium-latest-release.json")
  file(DOWNLOAD "${_release_api}" "${_release_metadata}"
    TLS_VERIFY ON
    HTTPHEADER
      "Accept: application/vnd.github+json"
      "X-GitHub-Api-Version: 2022-11-28"
      "User-Agent: QuantaPDF-CMake"
    STATUS _metadata_status)
  list(GET _metadata_status 0 _metadata_code)
  list(GET _metadata_status 1 _metadata_message)
  if(NOT _metadata_code EQUAL 0)
    file(REMOVE "${_release_metadata}")
    message(FATAL_ERROR "PDFium latest-release metadata download failed: ${_metadata_message}")
  endif()

  file(READ "${_release_metadata}" _release_json)
  string(JSON _release_tag GET "${_release_json}" tag_name)
  string(JSON _release_name GET "${_release_json}" name)
  string(JSON _asset_count LENGTH "${_release_json}" assets)
  if(_release_tag STREQUAL "" OR _release_name STREQUAL "" OR _asset_count EQUAL 0)
    message(FATAL_ERROR "PDFium latest-release metadata is incomplete")
  endif()

  string(REGEX REPLACE "^PDFium[ ]+" "" _expected_version "${_release_name}")
  if(_expected_version STREQUAL _release_name)
    message(FATAL_ERROR
      "PDFium latest release name does not expose a version: ${_release_name}")
  endif()

  set(_asset_name "pdfium-${_platform}.tgz")
  set(_url "")
  set(_asset_digest "")
  math(EXPR _asset_last "${_asset_count} - 1")
  foreach(_asset_index RANGE 0 ${_asset_last})
    string(JSON _candidate_name GET "${_release_json}" assets ${_asset_index} name)
    if(_candidate_name STREQUAL _asset_name)
      string(JSON _url GET "${_release_json}" assets ${_asset_index} browser_download_url)
      string(JSON _asset_digest GET "${_release_json}" assets ${_asset_index} digest)
      break()
    endif()
  endforeach()

  if(_url STREQUAL "")
    message(FATAL_ERROR
      "PDFium latest release ${_release_tag} does not provide ${_asset_name}")
  endif()
  if(NOT _asset_digest MATCHES "^sha256:[0-9A-Fa-f]{64}$")
    message(FATAL_ERROR
      "PDFium latest release ${_release_tag} does not provide a SHA-256 digest for ${_asset_name}")
  endif()
  string(REGEX REPLACE "^sha256:" "" _sha256 "${_asset_digest}")

  string(REGEX REPLACE "[^A-Za-z0-9._-]" "-" _release_key "${_release_tag}")
  set(_archive "${_download_directory}/pdfium-${_release_key}-${_platform}.tgz")
  set(_root "${CMAKE_BINARY_DIR}/_deps/pdfium-${_release_key}-${_platform}")

  if(NOT EXISTS "${_archive}")
    file(DOWNLOAD "${_url}" "${_archive}"
      EXPECTED_HASH "SHA256=${_sha256}"
      TLS_VERIFY ON
      STATUS _download_status)
    list(GET _download_status 0 _download_code)
    list(GET _download_status 1 _download_message)
    if(NOT _download_code EQUAL 0)
      file(REMOVE "${_archive}")
      message(FATAL_ERROR "PDFium download failed: ${_download_message}")
    endif()
  endif()

  file(SHA256 "${_archive}" _actual_sha256)
  if(NOT _actual_sha256 STREQUAL _sha256)
    file(REMOVE "${_archive}")
    message(FATAL_ERROR
      "Cached PDFium archive hash mismatch: expected ${_sha256}, "
      "got ${_actual_sha256}; the bad cache entry was removed")
  endif()

  if(NOT EXISTS "${_root}/VERSION")
    file(MAKE_DIRECTORY "${_root}")
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_root}")
  endif()

  foreach(_required_path IN ITEMS
      "include/fpdfview.h"
      "LICENSE"
      "licenses"
      "args.gn"
      "VERSION")
    if(NOT EXISTS "${_root}/${_required_path}")
      message(FATAL_ERROR
        "Latest PDFium artifact is missing ${_required_path}: ${_root}")
    endif()
  endforeach()

  file(READ "${_root}/args.gn" _build_arguments)
  foreach(_disabled_feature IN ITEMS
      "pdf_enable_v8 = false"
      "pdf_enable_xfa = false"
      "pdf_use_partition_alloc = false")
    string(FIND "${_build_arguments}" "${_disabled_feature}" _feature_position)
    if(_feature_position EQUAL -1)
      message(FATAL_ERROR
        "Latest PDFium artifact does not prove '${_disabled_feature}'")
    endif()
  endforeach()

  file(READ "${_root}/VERSION" _artifact_version)
  foreach(_component IN ITEMS MAJOR MINOR BUILD PATCH)
    string(REGEX MATCH "${_component}=([0-9]+)" _component_match "${_artifact_version}")
    if(_component_match STREQUAL "")
      message(FATAL_ERROR
        "Latest PDFium artifact has an invalid VERSION file: ${_root}/VERSION")
    endif()
    set(_artifact_${_component} "${CMAKE_MATCH_1}")
  endforeach()
  set(_artifact_version_string
      "${_artifact_MAJOR}.${_artifact_MINOR}.${_artifact_BUILD}.${_artifact_PATCH}")
  if(NOT _artifact_version_string STREQUAL _expected_version)
    message(FATAL_ERROR
      "PDFium latest release metadata/version mismatch: release=${_expected_version}, "
      "artifact=${_artifact_version_string}")
  endif()

  if(WIN32)
    set(_runtime "${_root}/bin/pdfium.dll")
    set(_import_library "${_root}/lib/pdfium.dll.lib")
    set(_runtime_directory "${_root}/bin")
  elseif(APPLE)
    set(_runtime "${_root}/lib/libpdfium.dylib")
    set(_runtime_directory "${_root}/lib")
  else()
    set(_runtime "${_root}/lib/libpdfium.so")
    set(_runtime_directory "${_root}/lib")
  endif()

  if(NOT EXISTS "${_runtime}")
    message(FATAL_ERROR "Latest PDFium runtime is missing: ${_runtime}")
  endif()
  if(WIN32 AND NOT EXISTS "${_import_library}")
    message(FATAL_ERROR "Latest PDFium import library is missing: ${_import_library}")
  endif()

  add_library(quantapdf_pdfium SHARED IMPORTED GLOBAL)
  add_library(QuantaPDF::PDFium ALIAS quantapdf_pdfium)
  set_target_properties(quantapdf_pdfium PROPERTIES
    IMPORTED_LOCATION "${_runtime}"
    INTERFACE_INCLUDE_DIRECTORIES "${_root}/include")
  if(WIN32)
    set_target_properties(quantapdf_pdfium PROPERTIES
      IMPORTED_IMPLIB "${_import_library}")
  endif()

  set(QUANTAPDF_PDFIUM_ROOT "${_root}" PARENT_SCOPE)
  set(QUANTAPDF_PDFIUM_RUNTIME_DIR "${_runtime_directory}" PARENT_SCOPE)
  set(QUANTAPDF_PDFIUM_LICENSE_DIR "${_root}/licenses" PARENT_SCOPE)
  set(QUANTAPDF_PDFIUM_VERSION "${_artifact_version_string}" PARENT_SCOPE)
  set(QUANTAPDF_PDFIUM_RELEASE "${_release_tag}" PARENT_SCOPE)
endfunction()
