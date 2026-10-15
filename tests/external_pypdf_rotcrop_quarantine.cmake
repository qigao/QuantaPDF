# #269: exact third-party CC-BY-SA original is QUARANTINED,
# never part of formal external source/Native positive counters.
# Every test is read-only and offline; installed SDK remains PDFium.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(QP_PYPDF_ROT_CROP_ORIGINAL
  "${PROJECT_SOURCE_DIR}/tests/external_geometry_candidates/pypdf-cropped-rotated-scaled.pdf")

add_test(NAME quantapdf.external_pypdf_rotcrop_original_geometry
  COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/check_external_pypdf_rotcrop_geometry.py"
    --source "${QP_PYPDF_ROT_CROP_ORIGINAL}"
    --report "${CMAKE_CURRENT_BINARY_DIR}/external-pypdf-rotcrop-raw-geometry.json")
set_tests_properties(quantapdf.external_pypdf_rotcrop_original_geometry
  PROPERTIES TIMEOUT 30
    LABELS "conformance;external-original;geometry-only;quarantine")

foreach(page_idx RANGE 0 3)
  add_test(NAME quantapdf.external_pypdf_rotcrop_preflight_page${page_idx}
    COMMAND "${CMAKE_COMMAND}"
      "-DORIGINAL_FILE=${QP_PYPDF_ROT_CROP_ORIGINAL}"
      "-DORACLE_EXECUTABLE=$<TARGET_FILE:quantapdf_test_native_source_qq_oracle>"
      "-DPAGE_INDEX=${page_idx}"
      "-DREPORT_FILE=${CMAKE_CURRENT_BINARY_DIR}/external-pypdf-rotcrop-page${page_idx}-typed-observe.json"
      -P "${PROJECT_SOURCE_DIR}/tests/check_external_pypdf_rotcrop_preflight.cmake")
  set_tests_properties(
    quantapdf.external_pypdf_rotcrop_preflight_page${page_idx}
    PROPERTIES TIMEOUT 45
      LABELS "conformance;external-original;source-preflight;quarantine")
  if(WIN32)
    set_property(TEST quantapdf.external_pypdf_rotcrop_preflight_page${page_idx}
      PROPERTY ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
  elseif(APPLE)
    set_property(TEST quantapdf.external_pypdf_rotcrop_preflight_page${page_idx}
      PROPERTY ENVIRONMENT_MODIFICATION
      "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
  else()
    set_property(TEST quantapdf.external_pypdf_rotcrop_preflight_page${page_idx}
      PROPERTY ENVIRONMENT_MODIFICATION
      "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
  endif()
endforeach()

# Real selected sealed Graph page geometry (not a PDF lexical shortcut):
# compare effective CropBox/Rotate/default UserUnit to independently pinned
# ORIGINAL boxes, then bound the four source corners to device bitmap edges.
# This is a private geometry-only observer; no native pixels or SDK change.
add_test(NAME quantapdf.external_pypdf_rotcrop_graph_device
  COMMAND "${CMAKE_COMMAND}"
    "-DORIGINAL_FILE=${QP_PYPDF_ROT_CROP_ORIGINAL}"
    "-DORACLE_EXECUTABLE=$<TARGET_FILE:quantapdf_test_native_source_qq_oracle>"
    "-DREPORT_FILE=${CMAKE_CURRENT_BINARY_DIR}/external-pypdf-rotcrop-graph-device.json"
    -P "${PROJECT_SOURCE_DIR}/tests/check_external_pypdf_graph_device.cmake")
set_tests_properties(quantapdf.external_pypdf_rotcrop_graph_device
  PROPERTIES TIMEOUT 45
    LABELS "conformance;external-original;sealed-graph;geometry-only;quarantine")
if(WIN32)
  set_property(TEST quantapdf.external_pypdf_rotcrop_graph_device
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.external_pypdf_rotcrop_graph_device
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.external_pypdf_rotcrop_graph_device
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# #269 original source stage trace: test default vs explicitly wide Graph
# capture budgets and *separately* observe Group, Content and Path lowering.
# Nothing reaches native Raster or PDFium original-pixel acceptance.
add_test(NAME quantapdf.external_pypdf_capture_stage_matrix
  COMMAND "${CMAKE_COMMAND}"
    "-DORIGINAL_FILE=${QP_PYPDF_ROT_CROP_ORIGINAL}"
    "-DORACLE_EXECUTABLE=$<TARGET_FILE:quantapdf_test_native_source_qq_oracle>"
    "-DREPORT_FILE=${CMAKE_CURRENT_BINARY_DIR}/external-pypdf-capture-stage-matrix.json"
    -P "${PROJECT_SOURCE_DIR}/tests/check_external_pypdf_capture_stage_matrix.cmake")
set_tests_properties(quantapdf.external_pypdf_capture_stage_matrix
  PROPERTIES TIMEOUT 60
  LABELS "conformance;external-original;capture-budget;stage-classification;quarantine")
if(WIN32)
  set_property(TEST quantapdf.external_pypdf_capture_stage_matrix
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.external_pypdf_capture_stage_matrix
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.external_pypdf_capture_stage_matrix
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# #269: independent ORIGINAL PDFium display dimensions at 72/96/144 DPI,
# compared with prior sealed Graph->C11 device extents. No native paint.
add_test(NAME quantapdf.external_pypdf_original_pdfium_dimensions
  COMMAND "${CMAKE_COMMAND}"
    "-DORIGINAL_FILE=${QP_PYPDF_ROT_CROP_ORIGINAL}"
    "-DORACLE_EXECUTABLE=$<TARGET_FILE:quantapdf_test_native_source_qq_oracle>"
    "-DREPORT_FILE=${CMAKE_CURRENT_BINARY_DIR}/external-pypdf-original-pdfium-dimensions.json"
    -P "${PROJECT_SOURCE_DIR}/tests/check_external_pypdf_pdfium_dimensions.cmake")
set_tests_properties(quantapdf.external_pypdf_original_pdfium_dimensions
  PROPERTIES TIMEOUT 75 LABELS
  "conformance;external-original;PDFium-reference;geometry-only;quarantine")
if(WIN32)
  set_property(TEST quantapdf.external_pypdf_original_pdfium_dimensions
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.external_pypdf_original_pdfium_dimensions
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.external_pypdf_original_pdfium_dimensions
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()
