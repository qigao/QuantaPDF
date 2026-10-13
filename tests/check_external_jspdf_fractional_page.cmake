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
string(JSON attr_group_bounds_len LENGTH "${json}" ordered_native_spatial_attribution changed_bounds_by_partial_mask)
string(JSON attr_near_bounds_len LENGTH "${json}" ordered_native_spatial_attribution flat_near_changed_bounds)
string(JSON attr_far_bounds_len LENGTH "${json}" ordered_native_spatial_attribution flat_far_changed_bounds)
string(JSON attr_far_worst_len LENGTH "${json}" ordered_native_spatial_attribution flat_far_worst_pixels)
string(JSON attr_near GET "${json}" ordered_native_spatial_attribution flat_changed_adjacent_partial_3x3)
string(JSON attr_far GET "${json}" ordered_native_spatial_attribution flat_changed_far_from_partial_3x3)
string(JSON attr_uncovered GET "${json}" ordered_native_spatial_attribution native_uncovered_reference_foreground)
if(NOT attr_schema EQUAL 1 OR NOT attr_mask_count EQUAL 4
   OR NOT attr_gross_count EQUAL 4 OR NOT attr_error_count EQUAL 4
   OR NOT attr_bound_len EQUAL 4 OR attr_worst_len GREATER 12
   OR NOT attr_group_bounds_len EQUAL 4
   OR NOT attr_near_bounds_len EQUAL 4
   OR NOT attr_far_bounds_len EQUAL 4 OR attr_far_worst_len GREATER 8)
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
  # Each per-category bbox is a four-number rectangle within source pixels.
  string(JSON attr_group_box_len LENGTH "${json}" ordered_native_spatial_attribution changed_bounds_by_partial_mask ${i})
  if(NOT attr_group_box_len EQUAL 4)
    message(FATAL_ERROR "Original jsPDF mismatch category bbox missing")
  endif()
  foreach(coord RANGE 0 3)
    string(JSON attr_group_box_value GET "${json}" ordered_native_spatial_attribution changed_bounds_by_partial_mask ${i} ${coord})
    if(attr_group_box_value LESS 0 OR
       ((coord EQUAL 0 OR coord EQUAL 2) AND attr_group_box_value GREATER 595) OR
       ((coord EQUAL 1 OR coord EQUAL 3) AND attr_group_box_value GREATER 841))
      message(FATAL_ERROR "Original jsPDF per-category bbox outside original page")
    endif()
  endforeach()

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
# The suspected native-flat, non-adjacent group must expose its own
# coordinates, without treating PDFium antialiasing as a proven explanation.
foreach(region IN ITEMS flat_near_changed_bounds flat_far_changed_bounds)
  foreach(coord RANGE 0 3)
    string(JSON edge GET "${json}" ordered_native_spatial_attribution ${region} ${coord})
    if(edge LESS 0 OR
       ((coord EQUAL 0 OR coord EQUAL 2) AND edge GREATER 595) OR
       ((coord EQUAL 1 OR coord EQUAL 3) AND edge GREATER 841))
      message(FATAL_ERROR "Original jsPDF flat mismatch region invalid")
    endif()
  endforeach()
endforeach()
if(attr_far GREATER 0 AND attr_far_worst_len EQUAL 0)
  message(FATAL_ERROR "Unexplained native-flat mismatch has no sample coordinates")
endif()
if(attr_far_worst_len GREATER 0)
  math(EXPR attr_far_last "${attr_far_worst_len}-1")
  set(attr_far_prior 256)
  foreach(i RANGE 0 ${attr_far_last})
    string(JSON x GET "${json}" ordered_native_spatial_attribution flat_far_worst_pixels ${i} x)
    string(JSON y GET "${json}" ordered_native_spatial_attribution flat_far_worst_pixels ${i} y)
    string(JSON delta GET "${json}" ordered_native_spatial_attribution flat_far_worst_pixels ${i} max_delta)
    string(JSON sa GET "${json}" ordered_native_spatial_attribution flat_far_worst_pixels ${i} stroke_alpha)
    string(JSON fa GET "${json}" ordered_native_spatial_attribution flat_far_worst_pixels ${i} fill_alpha)
    if(x LESS 0 OR x GREATER 595 OR y LESS 0 OR y GREATER 841
       OR delta LESS 1 OR delta GREATER attr_far_prior
       OR (sa GREATER 0 AND sa LESS 255)
       OR (fa GREATER 0 AND fa LESS 255))
      message(FATAL_ERROR "Original jsPDF native-flat far hotspot invalid")
    endif()
    set(attr_far_prior ${delta})
  endforeach()
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

# Experimental bounded axis-aligned 1pt Stroke preview is independently
# measured, NEVER promoted to whole-original positive admission.
foreach(field IN ITEMS status default_changed pixel_parity_qualified
    repeat_equal original_pdfium_pixel_count abs_rgb_error
    mean_abs_rgb_error changed_pixels gross_delta64_pixels
    max_channel_delta rgb_fnv64)
  string(JSON preview_${field} GET "${json}" horizontal_thin_1pt_preview_${field})
endforeach()
string(LENGTH "${preview_rgb_fnv64}" preview_digest_len)
if(NOT preview_status STREQUAL "measured_unqualified"
   OR preview_default_changed OR preview_pixel_parity_qualified
   OR NOT preview_repeat_equal
   OR NOT preview_original_pdfium_pixel_count EQUAL 501832
   OR preview_abs_rgb_error LESS 0
   OR preview_abs_rgb_error GREATER 383901480
   OR preview_mean_abs_rgb_error LESS 0
   OR preview_mean_abs_rgb_error GREATER 255
   OR preview_changed_pixels LESS 0
   OR preview_changed_pixels GREATER 501832
   OR preview_gross_delta64_pixels LESS 0
   OR preview_gross_delta64_pixels GREATER preview_changed_pixels
   OR preview_max_channel_delta LESS 0
   OR preview_max_channel_delta GREATER 255
   OR NOT preview_digest_len EQUAL 16
   OR NOT preview_rgb_fnv64 MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Preview candidate is unbounded or falsely pixel-qualified")
endif()

# Separate candidate spatial failure evidence: an actual full-page rescan,
# not just a curated list of large differences. Category sums must equal
# the independently measured Preview totals; no new pixel-positive is allowed.
string(JSON pr_schema GET "${json}" horizontal_thin_1pt_preview_spatial_attribution schema_version)
string(JSON pr_changed_len LENGTH "${json}" horizontal_thin_1pt_preview_spatial_attribution changed_by_partial_mask)
string(JSON pr_gross_len LENGTH "${json}" horizontal_thin_1pt_preview_spatial_attribution gross_by_partial_mask)
string(JSON pr_error_len LENGTH "${json}" horizontal_thin_1pt_preview_spatial_attribution abs_rgb_error_by_partial_mask)
string(JSON pr_bounds_len LENGTH "${json}" horizontal_thin_1pt_preview_spatial_attribution changed_bounds)
string(JSON pr_worst_len LENGTH "${json}" horizontal_thin_1pt_preview_spatial_attribution worst_pixels)
if(NOT pr_schema EQUAL 1 OR NOT pr_changed_len EQUAL 4
   OR NOT pr_gross_len EQUAL 4 OR NOT pr_error_len EQUAL 4
   OR NOT pr_bounds_len EQUAL 4 OR pr_worst_len GREATER 12)
  message(FATAL_ERROR "Original repeated-Stroke preview attribution incomplete")
endif()
set(pr_changed_total 0)
set(pr_gross_total 0)
set(pr_error_total 0)
foreach(i RANGE 0 3)
  string(JSON n GET "${json}" horizontal_thin_1pt_preview_spatial_attribution changed_by_partial_mask ${i})
  string(JSON g GET "${json}" horizontal_thin_1pt_preview_spatial_attribution gross_by_partial_mask ${i})
  string(JSON e GET "${json}" horizontal_thin_1pt_preview_spatial_attribution abs_rgb_error_by_partial_mask ${i})
  if(n LESS 0 OR n GREATER 501832 OR g LESS 0 OR g GREATER n
     OR e LESS 0 OR e GREATER 383901480)
    message(FATAL_ERROR "Preview group counts invalid")
  endif()
  math(EXPR pr_changed_total "${pr_changed_total}+${n}")
  math(EXPR pr_gross_total "${pr_gross_total}+${g}")
  math(EXPR pr_error_total "${pr_error_total}+${e}")
endforeach()
if(NOT pr_changed_total EQUAL preview_changed_pixels
   OR NOT pr_gross_total EQUAL preview_gross_delta64_pixels
   OR NOT pr_error_total EQUAL preview_abs_rgb_error)
  message(FATAL_ERROR "Preview spatial attribution contradicts whole-page totals")
endif()
if(preview_gross_delta64_pixels GREATER 0)
  if(pr_worst_len EQUAL 0)
    message(FATAL_ERROR "Preview large mismatches lack device coordinates")
  endif()
  string(JSON top_delta GET "${json}" horizontal_thin_1pt_preview_spatial_attribution worst_pixels 0 max_delta)
  if(NOT top_delta EQUAL preview_max_channel_delta)
    message(FATAL_ERROR "Preview worst coordinate does not match max delta")
  endif()
endif()
if(pr_worst_len GREATER 0)
  math(EXPR pr_last "${pr_worst_len}-1")
  set(pr_prior 256)
  foreach(i RANGE 0 ${pr_last})
    foreach(field IN ITEMS x y max_delta stroke_alpha fill_alpha)
      string(JSON pr_${field} GET "${json}" horizontal_thin_1pt_preview_spatial_attribution worst_pixels ${i} ${field})
    endforeach()
    if(pr_x LESS 0 OR pr_x GREATER 595
       OR pr_y LESS 0 OR pr_y GREATER 841
       OR pr_max_delta LESS 1 OR pr_max_delta GREATER pr_prior
       OR pr_stroke_alpha LESS 0 OR pr_stroke_alpha GREATER 255
       OR pr_fill_alpha LESS 0 OR pr_fill_alpha GREATER 255)
      message(FATAL_ERROR "Preview sorted original pixel coordinates malformed")
    endif()
    set(pr_prior ${pr_max_delta})
  endforeach()
endif()
# A real-original A/B acceptance gate: preview must improve BOTH total RGB
# error and gross-error count, without claiming per-pixel exact parity.
if(preview_abs_rgb_error GREATER_EQUAL cmp_ordered_native_composite_abs_rgb_error
   OR preview_gross_delta64_pixels GREATER_EQUAL
      cmp_ordered_native_composite_gross_delta64_pixels)
  message(FATAL_ERROR "Repeated-Stroke preview regressed pinned original image")
endif()

# Second independent endpoint-overlap A/B (still unqualified).
# A full original 596x842 rescan must agree with its spatial error
# decomposition and reported digest; NO guessed pixel-acceptance threshold.
foreach(field IN ITEMS status default_changed pixel_parity_qualified
    original_pdfium_pixel_count repeat_equal abs_rgb_error
    mean_abs_rgb_error changed_pixels gross_delta64_pixels
    max_channel_delta rgb_fnv64)
  string(JSON ep_${field} GET "${json}" horizontal_repeated_endpoint_preview_${field})
endforeach()
string(LENGTH "${ep_rgb_fnv64}" ep_digest_length)
if(NOT ep_status STREQUAL "measured_unqualified"
   OR ep_default_changed OR ep_pixel_parity_qualified
   OR NOT ep_original_pdfium_pixel_count EQUAL 501832
   OR NOT ep_repeat_equal
   OR ep_abs_rgb_error LESS 0 OR ep_abs_rgb_error GREATER 383901480
   OR ep_mean_abs_rgb_error LESS 0 OR ep_mean_abs_rgb_error GREATER 255
   OR ep_changed_pixels LESS 0 OR ep_changed_pixels GREATER 501832
   OR ep_gross_delta64_pixels LESS 0
   OR ep_gross_delta64_pixels GREATER ep_changed_pixels
   OR ep_max_channel_delta LESS 0 OR ep_max_channel_delta GREATER 255
   OR NOT ep_digest_length EQUAL 16
   OR NOT ep_rgb_fnv64 MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Unqualified repeated-endpoint trial is unbounded")
endif()
string(JSON ep_schema GET "${json}" horizontal_repeated_endpoint_preview_spatial_attribution schema_version)
string(JSON ep_changed_len LENGTH "${json}" horizontal_repeated_endpoint_preview_spatial_attribution changed_by_partial_mask)
string(JSON ep_gross_len LENGTH "${json}" horizontal_repeated_endpoint_preview_spatial_attribution gross_by_partial_mask)
string(JSON ep_error_len LENGTH "${json}" horizontal_repeated_endpoint_preview_spatial_attribution abs_rgb_error_by_partial_mask)
string(JSON ep_worst_len LENGTH "${json}" horizontal_repeated_endpoint_preview_spatial_attribution worst_pixels)
string(JSON ep_bounds_len LENGTH "${json}" horizontal_repeated_endpoint_preview_spatial_attribution changed_bounds)
if(NOT ep_schema EQUAL 1 OR NOT ep_changed_len EQUAL 4
   OR NOT ep_gross_len EQUAL 4 OR NOT ep_error_len EQUAL 4
   OR NOT ep_bounds_len EQUAL 4 OR ep_worst_len GREATER 12)
  message(FATAL_ERROR "Endpoint A/B whole-image attribution incomplete")
endif()
set(ep_sum_changed 0)
set(ep_sum_gross 0)
set(ep_sum_error 0)
foreach(i RANGE 0 3)
  string(JSON count GET "${json}" horizontal_repeated_endpoint_preview_spatial_attribution changed_by_partial_mask ${i})
  string(JSON severe GET "${json}" horizontal_repeated_endpoint_preview_spatial_attribution gross_by_partial_mask ${i})
  string(JSON error GET "${json}" horizontal_repeated_endpoint_preview_spatial_attribution abs_rgb_error_by_partial_mask ${i})
  if(count LESS 0 OR count GREATER 501832 OR severe LESS 0 OR severe GREATER count
     OR error LESS 0 OR error GREATER 383901480)
    message(FATAL_ERROR "Endpoint A/B attribution group malformed")
  endif()
  math(EXPR ep_sum_changed "${ep_sum_changed}+${count}")
  math(EXPR ep_sum_gross "${ep_sum_gross}+${severe}")
  math(EXPR ep_sum_error "${ep_sum_error}+${error}")
endforeach()
if(NOT ep_sum_changed EQUAL ep_changed_pixels
   OR NOT ep_sum_gross EQUAL ep_gross_delta64_pixels
   OR NOT ep_sum_error EQUAL ep_abs_rgb_error)
  message(FATAL_ERROR "Endpoint A/B whole-image counts contradict pixel origins")
endif()
if(ep_changed_pixels GREATER 0)
  if(ep_worst_len EQUAL 0)
    message(FATAL_ERROR "Endpoint A/B changed pixels lack reference coordinates")
  endif()
  string(JSON leading GET "${json}" horizontal_repeated_endpoint_preview_spatial_attribution worst_pixels 0 max_delta)
  if(NOT leading EQUAL ep_max_channel_delta)
    message(FATAL_ERROR "Endpoint A/B maximum channel delta not grounded")
  endif()
endif()
if(ep_worst_len GREATER 0)
  math(EXPR ep_last "${ep_worst_len}-1")
  set(ep_prior 256)
  foreach(i RANGE 0 ${ep_last})
    foreach(field IN ITEMS x y max_delta stroke_alpha fill_alpha)
      string(JSON ep_${field} GET "${json}" horizontal_repeated_endpoint_preview_spatial_attribution worst_pixels ${i} ${field})
    endforeach()
    if(ep_x LESS 0 OR ep_x GREATER 595 OR ep_y LESS 0 OR ep_y GREATER 841
       OR ep_max_delta LESS 1 OR ep_max_delta GREATER ep_prior
       OR ep_stroke_alpha LESS 0 OR ep_stroke_alpha GREATER 255
       OR ep_fill_alpha LESS 0 OR ep_fill_alpha GREATER 255)
      message(FATAL_ERROR "Endpoint A/B worst-site source coordinates invalid")
    endif()
    set(ep_prior ${ep_max_delta})
  endforeach()
endif()
# Deliberately do NOT demand endpoint trial improvement: it may regress.
# This stage establishes a falsifiable measurement, not a production rule.

# Third independent corner-overlap A/B (still unqualified).
# A full original 596x842 rescan must agree with its spatial error
# decomposition and reported digest; NO guessed pixel-acceptance threshold.
foreach(field IN ITEMS status default_changed pixel_parity_qualified
    original_pdfium_pixel_count repeat_equal abs_rgb_error
    mean_abs_rgb_error changed_pixels gross_delta64_pixels
    max_channel_delta rgb_fnv64)
  string(JSON corner_${field} GET "${json}" horizontal_right_down_corner_preview_${field})
endforeach()
string(LENGTH "${corner_rgb_fnv64}" corner_digest_length)
if(NOT corner_status STREQUAL "measured_unqualified"
   OR corner_default_changed OR corner_pixel_parity_qualified
   OR NOT corner_original_pdfium_pixel_count EQUAL 501832
   OR NOT corner_repeat_equal
   OR corner_abs_rgb_error LESS 0 OR corner_abs_rgb_error GREATER 383901480
   OR corner_mean_abs_rgb_error LESS 0 OR corner_mean_abs_rgb_error GREATER 255
   OR corner_changed_pixels LESS 0 OR corner_changed_pixels GREATER 501832
   OR corner_gross_delta64_pixels LESS 0
   OR corner_gross_delta64_pixels GREATER corner_changed_pixels
   OR corner_max_channel_delta LESS 0 OR corner_max_channel_delta GREATER 255
   OR NOT corner_digest_length EQUAL 16
   OR NOT corner_rgb_fnv64 MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Unqualified repeated-corner trial is unbounded")
endif()
string(JSON corner_schema GET "${json}" horizontal_right_down_corner_preview_spatial_attribution schema_version)
string(JSON corner_changed_len LENGTH "${json}" horizontal_right_down_corner_preview_spatial_attribution changed_by_partial_mask)
string(JSON corner_gross_len LENGTH "${json}" horizontal_right_down_corner_preview_spatial_attribution gross_by_partial_mask)
string(JSON corner_error_len LENGTH "${json}" horizontal_right_down_corner_preview_spatial_attribution abs_rgb_error_by_partial_mask)
string(JSON corner_worst_len LENGTH "${json}" horizontal_right_down_corner_preview_spatial_attribution worst_pixels)
string(JSON corner_bounds_len LENGTH "${json}" horizontal_right_down_corner_preview_spatial_attribution changed_bounds)
if(NOT corner_schema EQUAL 1 OR NOT corner_changed_len EQUAL 4
   OR NOT corner_gross_len EQUAL 4 OR NOT corner_error_len EQUAL 4
   OR NOT corner_bounds_len EQUAL 4 OR corner_worst_len GREATER 12)
  message(FATAL_ERROR "Right-angle corner A/B whole-image attribution incomplete")
endif()
set(corner_sum_changed 0)
set(corner_sum_gross 0)
set(corner_sum_error 0)
foreach(i RANGE 0 3)
  string(JSON count GET "${json}" horizontal_right_down_corner_preview_spatial_attribution changed_by_partial_mask ${i})
  string(JSON severe GET "${json}" horizontal_right_down_corner_preview_spatial_attribution gross_by_partial_mask ${i})
  string(JSON error GET "${json}" horizontal_right_down_corner_preview_spatial_attribution abs_rgb_error_by_partial_mask ${i})
  if(count LESS 0 OR count GREATER 501832 OR severe LESS 0 OR severe GREATER count
     OR error LESS 0 OR error GREATER 383901480)
    message(FATAL_ERROR "Right-angle corner A/B attribution group malformed")
  endif()
  math(EXPR corner_sum_changed "${corner_sum_changed}+${count}")
  math(EXPR corner_sum_gross "${corner_sum_gross}+${severe}")
  math(EXPR corner_sum_error "${corner_sum_error}+${error}")
endforeach()
if(NOT corner_sum_changed EQUAL corner_changed_pixels
   OR NOT corner_sum_gross EQUAL corner_gross_delta64_pixels
   OR NOT corner_sum_error EQUAL corner_abs_rgb_error)
  message(FATAL_ERROR "Right-angle corner A/B whole-image counts contradict pixel origins")
endif()
if(corner_changed_pixels GREATER 0)
  if(corner_worst_len EQUAL 0)
    message(FATAL_ERROR "Right-angle corner A/B changed pixels lack reference coordinates")
  endif()
  string(JSON leading GET "${json}" horizontal_right_down_corner_preview_spatial_attribution worst_pixels 0 max_delta)
  if(NOT leading EQUAL corner_max_channel_delta)
    message(FATAL_ERROR "Right-angle corner A/B maximum channel delta not grounded")
  endif()
endif()
if(corner_worst_len GREATER 0)
  math(EXPR corner_last "${corner_worst_len}-1")
  set(corner_prior 256)
  foreach(i RANGE 0 ${corner_last})
    foreach(field IN ITEMS x y max_delta stroke_alpha fill_alpha)
      string(JSON corner_${field} GET "${json}" horizontal_right_down_corner_preview_spatial_attribution worst_pixels ${i} ${field})
    endforeach()
    if(corner_x LESS 0 OR corner_x GREATER 595 OR corner_y LESS 0 OR corner_y GREATER 841
       OR corner_max_delta LESS 1 OR corner_max_delta GREATER corner_prior
       OR corner_stroke_alpha LESS 0 OR corner_stroke_alpha GREATER 255
       OR corner_fill_alpha LESS 0 OR corner_fill_alpha GREATER 255)
      message(FATAL_ERROR "Right-angle corner A/B worst-site source coordinates invalid")
    endif()
    set(corner_prior ${corner_max_delta})
  endforeach()
endif()
# Deliberately do NOT demand corner trial improvement: it may regress.
# This stage establishes a falsifiable measurement, not a production rule.

# Fourth-stage READ-ONLY residual partition: test the full image, not
# a few selected sites. No numeric threshold or candidate promotion.
string(JSON roi_schema GET "${json}" horizontal_right_down_corner_residual_regions schema_version)
string(JSON roi_observe GET "${json}" horizontal_right_down_corner_residual_regions observation_only)
string(JSON roi_parity GET "${json}" horizontal_right_down_corner_residual_regions pixel_parity_qualified)
string(JSON roi_reconciled GET "${json}" horizontal_right_down_corner_residual_regions full_image_partition_reconciled)
string(JSON roi_n LENGTH "${json}" horizontal_right_down_corner_residual_regions regions)
string(JSON roi_anchors LENGTH "${json}" horizontal_right_down_corner_residual_regions anchors)
if(NOT roi_schema EQUAL 1 OR NOT roi_observe OR roi_parity OR
   NOT roi_reconciled OR NOT roi_n EQUAL 7 OR NOT roi_anchors EQUAL 5)
  message(FATAL_ERROR "jsPDF original residual partition was not measured")
endif()
set(roi_regions bottom_single_pass_stroke right_vertical_stroke
    left_vertical_stroke top_repeated_stroke clip_right_antialias
    clip_bottom_weak_fringe other)
set(roi_pixels_sum 0)
set(roi_changed_sum 0)
set(roi_gross_sum 0)
set(roi_error_sum 0)
set(roi_max 0)
foreach(region IN LISTS roi_regions)
  foreach(field IN ITEMS pixels changed gross abs_rgb_error max_channel_delta worst_x worst_y)
    string(JSON roi_${field} GET "${json}" horizontal_right_down_corner_residual_regions regions ${region} ${field})
  endforeach()
  if(roi_pixels LESS 0 OR roi_pixels GREATER 501832
     OR roi_changed LESS 0 OR roi_changed GREATER roi_pixels
     OR roi_gross LESS 0 OR roi_gross GREATER roi_changed
     OR roi_abs_rgb_error LESS 0 OR roi_abs_rgb_error GREATER 383901480
     OR roi_max_channel_delta LESS 0 OR roi_max_channel_delta GREATER 255
     OR roi_worst_x LESS 0 OR roi_worst_x GREATER 595
     OR roi_worst_y LESS 0 OR roi_worst_y GREATER 841)
    message(FATAL_ERROR "jsPDF original residual region metric malformed")
  endif()
  math(EXPR roi_pixels_sum "${roi_pixels_sum}+${roi_pixels}")
  math(EXPR roi_changed_sum "${roi_changed_sum}+${roi_changed}")
  math(EXPR roi_gross_sum "${roi_gross_sum}+${roi_gross}")
  math(EXPR roi_error_sum "${roi_error_sum}+${roi_abs_rgb_error}")
  if(roi_max_channel_delta GREATER roi_max)
    set(roi_max "${roi_max_channel_delta}")
  endif()
endforeach()
if(NOT roi_pixels_sum EQUAL 501832
   OR NOT roi_changed_sum EQUAL corner_changed_pixels
   OR NOT roi_gross_sum EQUAL corner_gross_delta64_pixels
   OR NOT roi_error_sum EQUAL corner_abs_rgb_error
   OR NOT roi_max EQUAL corner_max_channel_delta)
  message(FATAL_ERROR "jsPDF seven error regions fail full-original reconciliation")
endif()
set(roi_anchor_names bottom_left bottom_right clip_right clip_bottom repeated_corner)
set(roi_anchor_groups bottom_single_pass_stroke bottom_single_pass_stroke
    clip_right_antialias clip_bottom_weak_fringe top_repeated_stroke)
set(roi_anchor_x 50 250 150 100 250)
set(roi_anchor_y 139 139 21 100 19)
foreach(i RANGE 0 4)
  list(GET roi_anchor_names ${i} expected_name)
  list(GET roi_anchor_groups ${i} expected_group)
  list(GET roi_anchor_x ${i} expected_x)
  list(GET roi_anchor_y ${i} expected_y)
  foreach(field IN ITEMS name region x y stroke_alpha fill_alpha)
    string(JSON anchor_${field} GET "${json}" horizontal_right_down_corner_residual_regions anchors ${i} ${field})
  endforeach()
  string(JSON rgb_len LENGTH "${json}" horizontal_right_down_corner_residual_regions anchors ${i} native_rgb)
  string(JSON pdf_len LENGTH "${json}" horizontal_right_down_corner_residual_regions anchors ${i} pdfium_rgb)
  if(NOT anchor_name STREQUAL expected_name
     OR NOT anchor_region STREQUAL expected_group
     OR NOT anchor_x EQUAL expected_x OR NOT anchor_y EQUAL expected_y
     OR anchor_stroke_alpha LESS 0 OR anchor_stroke_alpha GREATER 255
     OR anchor_fill_alpha LESS 0 OR anchor_fill_alpha GREATER 255
     OR NOT rgb_len EQUAL 3 OR NOT pdf_len EQUAL 3)
    message(FATAL_ERROR "jsPDF residual original-anchor classification invalid")
  endif()
  foreach(c RANGE 0 2)
    string(JSON native GET "${json}" horizontal_right_down_corner_residual_regions anchors ${i} native_rgb ${c})
    string(JSON oracle GET "${json}" horizontal_right_down_corner_residual_regions anchors ${i} pdfium_rgb ${c})
    if(native LESS 0 OR native GREATER 255 OR oracle LESS 0 OR oracle GREATER 255)
      message(FATAL_ERROR "jsPDF residual site RGB malformed")
    endif()
  endforeach()
endforeach()


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
message(STATUS "jsPDF Stroke 1pt horizontal preview: NOT certified, original_count=0 baseline_abs=${cmp_ordered_native_composite_abs_rgb_error} preview_abs=${preview_abs_rgb_error} baseline_gross=${cmp_ordered_native_composite_gross_delta64_pixels} preview_gross=${preview_gross_delta64_pixels}")
message(STATUS "jsPDF Stroke repeated endpoint A/B: NOT certified, interior_preview_abs=${preview_abs_rgb_error} endpoint_abs=${ep_abs_rgb_error} interior_gross=${preview_gross_delta64_pixels} endpoint_gross=${ep_gross_delta64_pixels}")
message(STATUS "jsPDF Stroke right-angle corner A/B: NOT certified, endpoint_abs=${ep_abs_rgb_error} corner_abs=${corner_abs_rgb_error} endpoint_gross=${ep_gross_delta64_pixels} corner_gross=${corner_gross_delta64_pixels}")
