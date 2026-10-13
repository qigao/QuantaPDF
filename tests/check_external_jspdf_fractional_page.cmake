# Exact published jsPDF W-only original, negative native raster admission.
cmake_minimum_required(VERSION 3.24)
foreach(arg IN ITEMS ORACLE SOURCE REPORT EXPECTED_SHA256)
  if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${arg}")
  endif()
endforeach()
file(SIZE "${SOURCE}" bytes)
if(NOT bytes EQUAL 3344)
  message(FATAL_ERROR "Original jsPDF page byte size changed")
endif()
file(SHA256 "${SOURCE}" before)
if(NOT before STREQUAL EXPECTED_SHA256)
  message(FATAL_ERROR "Original jsPDF SHA256 changed")
endif()
execute_process(
  COMMAND "${ORACLE}" --external-fractional-jspdf-pdf "${SOURCE}"
    --external-report "${REPORT}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "Raw jsPDF fractional selected-page negative failed: ${rc}: ${stderr}")
endif()
file(READ "${REPORT}" json)
foreach(field IN ITEMS result typed_native_lower_status native_raster_status
    native_raster_rejection_reason original_pdfium_pixel_oracle
    original_pdfium_geometry_oracle)
  string(JSON got_${field} GET "${json}" ${field})
endforeach()
string(JSON device_width GET "${json}" original_pdfium_device_width)
string(JSON device_height GET "${json}" original_pdfium_device_height)
string(JSON dpi GET "${json}" original_pdfium_default_dpi)
string(JSON width GET "${json}" page_width_points)
string(JSON height GET "${json}" page_height_points)
string(JSON paths GET "${json}" detached_ir_painted_paths)
string(JSON w GET "${json}" detached_ir_w_nonzero_clip_leaves)
string(JSON wstar GET "${json}" detached_ir_wstar_clip_leaves)
string(JSON observed GET "${json}" observation_only)
string(JSON released GET "${json}" source_and_graph_released_before_raster)
string(JSON partial GET "${json}" native_partial_pixels_published)
string(JSON optin_grid GET "${json}" optin_grid_dimensions_qualified)
string(JSON optin_status GET "${json}" optin_native_raster_status)
string(JSON optin_class GET "${json}" optin_rejection_classification)
string(JSON optin_strokes GET "${json}" optin_original_stroke_paths)
string(JSON optin_fills GET "${json}" optin_original_fill_paths)
string(JSON optin_partial GET "${json}" optin_native_partial_pixels_published)
string(JSON stroke_status GET "${json}" isolated_original_stroke_native_status)
string(JSON stroke_anchors GET "${json}" isolated_original_stroke_native_nonzero_anchors)
string(JSON stroke_pdfium GET "${json}" isolated_original_stroke_pdfium_anchor_observations)
string(JSON stroke_repeat GET "${json}" isolated_original_stroke_native_repeat_equal)
string(JSON stroke_full_positive GET "${json}" isolated_original_stroke_is_full_page_positive)
string(JSON fill_status GET "${json}" isolated_original_fill_native_status)
string(JSON fill_clip_count GET "${json}" isolated_original_fill_native_clip_count)
string(JSON fill_pdfium GET "${json}" isolated_original_fill_pdfium_anchor_observations)
string(JSON fill_repeat GET "${json}" isolated_original_fill_native_repeat_equal)
string(JSON fill_full_positive GET "${json}" isolated_original_fill_is_full_page_positive)
string(JSON positive GET "${json}" native_pixel_positive_count)
string(JSON admitted GET "${json}" official_corpus_admitted)
string(JSON generated GET "${json}" generated_fixture_count)
if(NOT got_result STREQUAL "quarantined_original_fractional_page_native_rejection"
   OR NOT got_typed_native_lower_status STREQUAL "lowered"
   OR NOT got_native_raster_status STREQUAL "unsupported"
   OR NOT got_native_raster_rejection_reason STREQUAL "fractional_page_points"
   OR NOT got_original_pdfium_pixel_oracle STREQUAL "not_run"
   OR NOT got_original_pdfium_geometry_oracle STREQUAL "passed"
   OR NOT device_width EQUAL 596 OR NOT device_height EQUAL 842
   OR NOT dpi EQUAL 72
   OR NOT width EQUAL 595.28 OR NOT height EQUAL 841.89
   OR NOT paths EQUAL 2 OR NOT w EQUAL 1 OR NOT wstar EQUAL 0
   OR NOT optin_grid OR NOT optin_status STREQUAL "unsupported"
   OR NOT optin_class STREQUAL "stroked_source_not_admitted"
   OR NOT stroke_status STREQUAL "rasterized_candidate"
   OR NOT stroke_anchors EQUAL 2
   OR NOT stroke_pdfium STREQUAL "passed"
   OR NOT stroke_repeat OR stroke_full_positive
   OR NOT fill_status STREQUAL "rasterized_candidate"
   OR NOT fill_clip_count EQUAL 1
   OR NOT fill_pdfium STREQUAL "passed"
   OR NOT fill_repeat OR fill_full_positive
   OR optin_strokes LESS 1 OR optin_fills LESS 1 OR optin_partial
   OR NOT observed OR NOT released OR partial OR positive OR admitted OR generated)
  message(FATAL_ERROR "Fractional page refusal was not complete and correctly classified")
endif()
file(SHA256 "${SOURCE}" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "Original jsPDF source mutated")
endif()
message(STATUS "fractional-page-jsPDF: typed=lowered raster=UNSUPPORTED original_sha256=${after} official=0")
