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
string(JSON cmp_ordered_native_composite_status GET "${json}" ordered_native_composite_status)
string(JSON cmp_ordered_native_composite_sequence GET "${json}" ordered_native_composite_sequence)
string(JSON cmp_ordered_native_composite_repeat_equal GET "${json}" ordered_native_composite_repeat_equal)
string(JSON cmp_ordered_native_composite_rgba_is_candidate GET "${json}" ordered_native_composite_rgba_is_candidate)
string(JSON cmp_ordered_native_composite_pixel_parity_qualified GET "${json}" ordered_native_composite_pixel_parity_qualified)
string(JSON cmp_ordered_native_composite_pixel_count GET "${json}" ordered_native_composite_pixel_count)
string(JSON cmp_ordered_native_composite_rgb_samples GET "${json}" ordered_native_composite_rgb_samples)
string(JSON cmp_ordered_native_composite_abs_rgb_error GET "${json}" ordered_native_composite_abs_rgb_error)
string(JSON cmp_ordered_native_composite_mean_abs_rgb_error GET "${json}" ordered_native_composite_mean_abs_rgb_error)
string(JSON cmp_ordered_native_composite_max_channel_delta GET "${json}" ordered_native_composite_max_channel_delta)
string(JSON cmp_ordered_native_composite_changed_pixels GET "${json}" ordered_native_composite_changed_pixels)
string(JSON cmp_ordered_native_composite_gross_delta64_pixels GET "${json}" ordered_native_composite_gross_delta64_pixels)
string(JSON cmp_ordered_native_composite_partial_coverage_pixels GET "${json}" ordered_native_composite_partial_coverage_pixels)
string(JSON cmp_ordered_native_composite_partial_coverage_changed_pixels GET "${json}" ordered_native_composite_partial_coverage_changed_pixels)
string(JSON cmp_ordered_native_composite_flat_coverage_changed_pixels GET "${json}" ordered_native_composite_flat_coverage_changed_pixels)
string(JSON cmp_ordered_native_composite_rgb_fnv64 GET "${json}" ordered_native_composite_rgb_fnv64)

# Independently recomputed bounded spatial attribution of exact same pixels.
string(JSON attr_schema GET "${json}" ordered_native_spatial_attribution schema_version)
string(JSON attr_mask_count LENGTH "${json}" ordered_native_spatial_attribution changed_by_partial_mask)
string(JSON attr_gross_count LENGTH "${json}" ordered_native_spatial_attribution gross_by_partial_mask)
string(JSON attr_error_count LENGTH "${json}" ordered_native_spatial_attribution abs_rgb_error_by_partial_mask)
string(JSON attr_bound_len LENGTH "${json}" ordered_native_spatial_attribution changed_bounds)
string(JSON attr_worst_len LENGTH "${json}" ordered_native_spatial_attribution worst_pixels)
string(JSON attr_near GET "${json}" ordered_native_spatial_attribution flat_changed_adjacent_partial_3x3)
string(JSON attr_far GET "${json}" ordered_native_spatial_attribution flat_changed_far_from_partial_3x3)
string(JSON attr_uncovered GET "${json}" ordered_native_spatial_attribution native_uncovered_reference_foreground)
if(NOT attr_schema EQUAL 1 OR NOT attr_mask_count EQUAL 4
   OR NOT attr_gross_count EQUAL 4 OR NOT attr_error_count EQUAL 4
   OR NOT attr_bound_len EQUAL 4 OR attr_worst_len GREATER 12)
  message(FATAL_ERROR "Original jsPDF spatial attribution shape malformed")
endif()
set(attr_changed_sum 0)
set(attr_gross_sum 0)
set(attr_error_sum 0)
foreach(i RANGE 0 3)
  string(JSON attr_changed GET "${json}" ordered_native_spatial_attribution changed_by_partial_mask ${i})
  string(JSON attr_gross GET "${json}" ordered_native_spatial_attribution gross_by_partial_mask ${i})
  string(JSON attr_error GET "${json}" ordered_native_spatial_attribution abs_rgb_error_by_partial_mask ${i})
  if(attr_changed LESS 0 OR attr_gross LESS 0 OR attr_error LESS 0
     OR attr_gross GREATER attr_changed)
    message(FATAL_ERROR "Original jsPDF attribution negative/out-of-range counts")
  endif()
  math(EXPR attr_changed_sum "${attr_changed_sum}+${attr_changed}")
  math(EXPR attr_gross_sum "${attr_gross_sum}+${attr_gross}")
  math(EXPR attr_error_sum "${attr_error_sum}+${attr_error}")
  if(i EQUAL 0)
    set(attr_flat ${attr_changed})
  endif()
endforeach()
math(EXPR attr_flat_decomposition "${attr_near}+${attr_far}")
if(NOT attr_changed_sum EQUAL cmp_ordered_native_composite_changed_pixels
   OR NOT attr_gross_sum EQUAL cmp_ordered_native_composite_gross_delta64_pixels
   OR NOT attr_error_sum EQUAL cmp_ordered_native_composite_abs_rgb_error
   OR NOT attr_flat EQUAL cmp_ordered_native_composite_flat_coverage_changed_pixels
   OR NOT attr_flat_decomposition EQUAL attr_flat
   OR attr_uncovered LESS 0 OR attr_uncovered GREATER attr_changed_sum)
  message(FATAL_ERROR "Original jsPDF attribution disagrees with full-image metrics")
endif()
foreach(i RANGE 0 3)
  string(JSON bound GET "${json}" ordered_native_spatial_attribution changed_bounds ${i})
  if(bound LESS 0 OR (i EQUAL 0 AND bound GREATER 595)
     OR (i EQUAL 2 AND bound GREATER 595)
     OR (i EQUAL 1 AND bound GREATER 841)
     OR (i EQUAL 3 AND bound GREATER 841))
    message(FATAL_ERROR "Original jsPDF attribution bounds invalid")
  endif()
endforeach()
if(cmp_ordered_native_composite_changed_pixels GREATER 0)
  if(attr_worst_len EQUAL 0)
    message(FATAL_ERROR "Original jsPDF changed pixels missing worst-site proof")
  endif()
  string(JSON attr_worst_max GET "${json}" ordered_native_spatial_attribution worst_pixels 0 max_delta)
  if(NOT attr_worst_max EQUAL cmp_ordered_native_composite_max_channel_delta)
    message(FATAL_ERROR "Original jsPDF worst-site mismatch")
  endif()
endif()
if(attr_worst_len GREATER 0)
  math(EXPR attr_last "${attr_worst_len}-1")
  set(attr_prior 256)
  foreach(i RANGE 0 ${attr_last})
    string(JSON x GET "${json}" ordered_native_spatial_attribution worst_pixels ${i} x)
    string(JSON y GET "${json}" ordered_native_spatial_attribution worst_pixels ${i} y)
    string(JSON delta GET "${json}" ordered_native_spatial_attribution worst_pixels ${i} max_delta)
    if(x GREATER 595 OR x LESS 0 OR y GREATER 841 OR y LESS 0
       OR delta LESS 1 OR delta GREATER attr_prior)
      message(FATAL_ERROR "Original jsPDF worst-site coordinates/ranking invalid")
    endif()
    set(attr_prior ${delta})
  endforeach()
endif()

string(JSON positive GET "${json}" native_pixel_positive_count)
string(JSON admitted GET "${json}" official_corpus_admitted)
string(JSON generated GET "${json}" generated_fixture_count)
if(NOT got_result STREQUAL "quarantined_original_fractional_page_native_rejection"
   OR NOT got_typed_native_lower_status STREQUAL "lowered"
   OR NOT got_native_raster_status STREQUAL "unsupported"
   OR NOT got_native_raster_rejection_reason STREQUAL "fractional_page_points"
   OR NOT got_original_pdfium_pixel_oracle STREQUAL "compared_diagnostic_unqualified"
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
   OR NOT cmp_ordered_native_composite_status STREQUAL "full_image_measured_diagnostic"
   OR NOT cmp_ordered_native_composite_sequence STREQUAL "stroke_then_w_clipped_fill"
   OR NOT cmp_ordered_native_composite_repeat_equal
   OR NOT cmp_ordered_native_composite_rgba_is_candidate
   OR cmp_ordered_native_composite_pixel_parity_qualified
   OR NOT cmp_ordered_native_composite_pixel_count EQUAL 501832
   OR NOT cmp_ordered_native_composite_rgb_samples EQUAL 1505496
   OR cmp_ordered_native_composite_abs_rgb_error LESS 0
   OR cmp_ordered_native_composite_mean_abs_rgb_error LESS 0
   OR cmp_ordered_native_composite_mean_abs_rgb_error GREATER 255
   OR cmp_ordered_native_composite_max_channel_delta LESS 0
   OR cmp_ordered_native_composite_max_channel_delta GREATER 255
   OR cmp_ordered_native_composite_changed_pixels GREATER 501832
   OR cmp_ordered_native_composite_gross_delta64_pixels GREATER
      cmp_ordered_native_composite_changed_pixels
   OR cmp_ordered_native_composite_partial_coverage_pixels GREATER 501832
   OR cmp_ordered_native_composite_partial_coverage_changed_pixels GREATER
      cmp_ordered_native_composite_partial_coverage_pixels

   OR optin_strokes LESS 1 OR optin_fills LESS 1 OR optin_partial
   OR NOT observed OR NOT released OR partial OR positive OR admitted OR generated)
  message(FATAL_ERROR "Fractional page refusal was not complete and correctly classified")
endif()
# The pixel diagnostic is a measurement, NOT a pixel-parity pass.
# Reject untrusted/overstated report lengths and non-hex fingerprints.
math(EXPR composed_changed_decomposition
  "${cmp_ordered_native_composite_partial_coverage_changed_pixels}+${cmp_ordered_native_composite_flat_coverage_changed_pixels}")
if(NOT composed_changed_decomposition EQUAL cmp_ordered_native_composite_changed_pixels)
  message(FATAL_ERROR "Diagnostic mismatch decomposition is inconsistent")
endif()
string(LENGTH "${cmp_ordered_native_composite_rgb_fnv64}" fnv_len)
if(NOT fnv_len EQUAL 16 OR
   NOT cmp_ordered_native_composite_rgb_fnv64 MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Diagnostic FNV-64 fingerprint missing or malformed")
endif()
file(SHA256 "${SOURCE}" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "Original jsPDF source mutated")
endif()
message(STATUS "fractional-page-jsPDF: typed=lowered raster=UNSUPPORTED original_sha256=${after} official=0 whole_image_diagnostic_mean_rgb=${cmp_ordered_native_composite_mean_abs_rgb_error} gross_delta64=${cmp_ordered_native_composite_gross_delta64_pixels}")
