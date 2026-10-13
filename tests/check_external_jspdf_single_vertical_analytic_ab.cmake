# Original upstream jsPDF vertical coverage A/B, test-only and quarantined.
cmake_minimum_required(VERSION 3.24)
foreach(required IN ITEMS ORACLE SOURCE REPORT EXPECTED_SHA256)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${required}")
  endif()
endforeach()
file(SIZE "${SOURCE}" nbytes)
file(SHA256 "${SOURCE}" before)
if(NOT nbytes EQUAL 3344 OR NOT before STREQUAL EXPECTED_SHA256)
  message(FATAL_ERROR "Immutable original jsPDF identity has changed")
endif()
execute_process(
  COMMAND "${ORACLE}" --external-jspdf-vertical-analytic-ab "${SOURCE}"
    --external-report "${REPORT}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "Original jsPDF vertical analytic A/B failed: ${rc}: ${stderr}")
endif()
file(READ "${REPORT}" json)
foreach(field IN ITEMS schema_version result document_id observation_only
    pixel_parity_qualified default_renderer_changed source_and_graph_released
    original_pdfium_width original_pdfium_height original_pdfium_pixel_count
    original_rgb_channels paint_order candidate_mode candidate_repeat_equal
    source_stroke_alpha_changed_pixels
    source_stroke_changes_outside_vertical_interior
    mode3_abs_rgb_error mode3_changed_pixels mode3_gross_delta64_pixels
    mode3_max_channel_delta mode3_rgb_fnv64
    mode4_abs_rgb_error mode4_mean_abs_rgb_error
    mode4_changed_pixels mode4_gross_delta64_pixels mode4_max_channel_delta
    mode4_rgb_fnv64 native_original_pixel_positive_count
    official_w_wstar_positive_count_increase)
  string(JSON got_${field} GET "${json}" ${field})
endforeach()
string(LENGTH "${got_mode3_rgb_fnv64}" baseline_digest_length)
string(LENGTH "${got_mode4_rgb_fnv64}" trial_digest_length)
if(NOT got_schema_version EQUAL 1
   OR NOT got_result STREQUAL "original_jspdf_private_vertical_analytic_ab"
   OR NOT got_document_id STREQUAL "jspdf.w3s-clip-v1"
   OR NOT got_observation_only OR got_pixel_parity_qualified
   OR got_default_renderer_changed OR NOT got_source_and_graph_released
   OR NOT got_original_pdfium_width EQUAL 596
   OR NOT got_original_pdfium_height EQUAL 842
   OR NOT got_original_pdfium_pixel_count EQUAL 501832
   OR NOT got_original_rgb_channels EQUAL 1505496
   OR NOT got_paint_order STREQUAL "stroke_then_w_clipped_fill"
   OR NOT got_candidate_mode STREQUAL "explicit_private_single_vertical_analytic"
   OR NOT got_candidate_repeat_equal
   OR got_source_stroke_alpha_changed_pixels LESS 1
   OR got_source_stroke_alpha_changed_pixels GREATER 501832
   OR NOT got_source_stroke_changes_outside_vertical_interior EQUAL 0
   OR NOT baseline_digest_length EQUAL 16
   OR NOT trial_digest_length EQUAL 16
   OR NOT got_mode3_rgb_fnv64 MATCHES "^[0-9a-f]+$"
   OR NOT got_mode4_rgb_fnv64 MATCHES "^[0-9a-f]+$"
   OR NOT got_native_original_pixel_positive_count EQUAL 0
   OR NOT got_official_w_wstar_positive_count_increase EQUAL 0)
  message(FATAL_ERROR "Quarantined private vertical A/B status, bounds or scope invalid")
endif()
# Freeze only the CI-qualified #287/#288 baseline, never the hypothesis.
if(NOT got_mode3_abs_rgb_error EQUAL 26955
   OR NOT got_mode3_changed_pixels EQUAL 1060
   OR NOT got_mode3_gross_delta64_pixels EQUAL 0
   OR NOT got_mode3_max_channel_delta EQUAL 38
   OR NOT got_mode3_rgb_fnv64 STREQUAL "1de2c62251fd22c0")
  message(FATAL_ERROR "Mode-3 verified original-page baseline mutated")
endif()
if(got_mode4_abs_rgb_error LESS 0 OR got_mode4_abs_rgb_error GREATER 383901480
   OR got_mode4_mean_abs_rgb_error LESS 0
   OR got_mode4_mean_abs_rgb_error GREATER 255
   OR got_mode4_changed_pixels LESS 0
   OR got_mode4_changed_pixels GREATER 501832
   OR got_mode4_gross_delta64_pixels LESS 0
   OR got_mode4_gross_delta64_pixels GREATER got_mode4_changed_pixels
   OR got_mode4_max_channel_delta LESS 0
   OR got_mode4_max_channel_delta GREATER 255)
  message(FATAL_ERROR "Mode-4 full-original statistics out of range")
endif()

# Read-only mode4 original-page region report. Exactly seven disjoint
# real source-specific buckets and eight coordinate-anchored observations.
string(JSON roi_schema GET "${json}" mode4_original_source_regions schema_version)
string(JSON roi_observe GET "${json}" mode4_original_source_regions observation_only)
string(JSON roi_parity GET "${json}" mode4_original_source_regions pixel_parity_qualified)
string(JSON roi_ok GET "${json}" mode4_original_source_regions full_original_reconciled)
string(JSON roi_count LENGTH "${json}" mode4_original_source_regions regions)
string(JSON wit_count LENGTH "${json}" mode4_original_source_regions witnesses)
if(NOT roi_schema EQUAL 1 OR NOT roi_observe OR roi_parity OR
   NOT roi_ok OR NOT roi_count EQUAL 7 OR NOT wit_count EQUAL 8)
  message(FATAL_ERROR "Missing or overstated mode4 original source-region audit")
endif()
set(roi_names bottom_single_horizontal_stroke right_vertical_stroke
    left_vertical_stroke top_repeated_stroke clip_right_red_boundary
    clip_bottom_weak_fringe other)
set(sum_pixels 0)
set(sum_changed 0)
set(sum_gross 0)
set(sum_error 0)
set(sum_brighter 0)
set(sum_darker 0)
set(max_delta 0)
foreach(name IN LISTS roi_names)
  foreach(field IN ITEMS pixels changed gross abs_rgb_error
      brighter_rgb_error darker_rgb_error max_delta worst_x worst_y)
    string(JSON r_${field} GET "${json}"
      mode4_original_source_regions regions ${name} ${field})
  endforeach()
  math(EXPR biased "${r_brighter_rgb_error}+${r_darker_rgb_error}")
  if(r_pixels LESS 0 OR r_pixels GREATER 501832
     OR r_changed LESS 0 OR r_changed GREATER r_pixels
     OR r_gross LESS 0 OR r_gross GREATER r_changed
     OR r_abs_rgb_error LESS 0 OR r_abs_rgb_error GREATER 383901480
     OR NOT biased EQUAL r_abs_rgb_error
     OR r_max_delta LESS 0 OR r_max_delta GREATER 255
     OR r_worst_x LESS 0 OR r_worst_x GREATER 595
     OR r_worst_y LESS 0 OR r_worst_y GREATER 841)
    message(FATAL_ERROR "Mode4 source-region counter exceeds bounds")
  endif()
  math(EXPR sum_pixels "${sum_pixels}+${r_pixels}")
  math(EXPR sum_changed "${sum_changed}+${r_changed}")
  math(EXPR sum_gross "${sum_gross}+${r_gross}")
  math(EXPR sum_error "${sum_error}+${r_abs_rgb_error}")
  math(EXPR sum_brighter "${sum_brighter}+${r_brighter_rgb_error}")
  math(EXPR sum_darker "${sum_darker}+${r_darker_rgb_error}")
  if(r_max_delta GREATER max_delta)
    set(max_delta "${r_max_delta}")
  endif()
endforeach()
math(EXPR signed_errors "${sum_brighter}+${sum_darker}")
if(NOT sum_pixels EQUAL 501832 OR NOT sum_changed EQUAL got_mode4_changed_pixels
   OR NOT sum_gross EQUAL got_mode4_gross_delta64_pixels
   OR NOT sum_error EQUAL got_mode4_abs_rgb_error
   OR NOT signed_errors EQUAL got_mode4_abs_rgb_error
   OR NOT max_delta EQUAL got_mode4_max_channel_delta)
  message(FATAL_ERROR "Mode4 residual category totals contradict full PDF")
endif()
set(w_names bottom_left bottom_interior bottom_right
    right_vertical left_vertical top_corner clip_right clip_bottom)
set(w_regions bottom_single_horizontal_stroke
    bottom_single_horizontal_stroke bottom_single_horizontal_stroke
    right_vertical_stroke left_vertical_stroke top_repeated_stroke
    clip_right_red_boundary clip_bottom_weak_fringe)
set(w_expected_x 50 100 250 250 50 250 150 100)
set(w_expected_y 139 139 139 60 110 19 40 100)
foreach(i RANGE 0 7)
  list(GET w_names ${i} expected_name)
  list(GET w_regions ${i} expected_region)
  list(GET w_expected_x ${i} expected_x)
  list(GET w_expected_y ${i} expected_y)
  foreach(field IN ITEMS name region x y stroke_alpha fill_alpha)
    string(JSON w_${field} GET "${json}"
      mode4_original_source_regions witnesses ${i} ${field})
  endforeach()
  string(JSON rn LENGTH "${json}" mode4_original_source_regions witnesses ${i} native_rgb)
  string(JSON ro LENGTH "${json}" mode4_original_source_regions witnesses ${i} pdfium_rgb)
  if(NOT w_name STREQUAL expected_name OR NOT w_region STREQUAL expected_region
     OR NOT w_x EQUAL expected_x OR NOT w_y EQUAL expected_y
     OR w_stroke_alpha LESS 0 OR w_stroke_alpha GREATER 255
     OR w_fill_alpha LESS 0 OR w_fill_alpha GREATER 255
     OR NOT rn EQUAL 3 OR NOT ro EQUAL 3)
    message(FATAL_ERROR "Mode4 original witness coordinates/category invalid")
  endif()
  foreach(ch RANGE 0 2)
    string(JSON native GET "${json}" mode4_original_source_regions witnesses ${i} native_rgb ${ch})
    string(JSON oracle GET "${json}" mode4_original_source_regions witnesses ${i} pdfium_rgb ${ch})
    if(native LESS 0 OR native GREATER 255 OR oracle LESS 0 OR oracle GREATER 255)
      message(FATAL_ERROR "Mode4 original witness RGB invalid")
    endif()
  endforeach()
endforeach()
# Purely read-only branch: the verified 289 candidate cannot change.
if(NOT got_mode4_abs_rgb_error EQUAL 10979
   OR NOT got_mode4_changed_pixels EQUAL 791
   OR NOT got_mode4_gross_delta64_pixels EQUAL 0
   OR NOT got_mode4_max_channel_delta EQUAL 38
   OR NOT got_mode4_rgb_fnv64 STREQUAL "226faee70471ea2c")
  message(FATAL_ERROR "Verified mode4 candidate output unexpectedly changed")
endif()


# Independent fifth PRIVATE C11 experiment on the unchanged original
# PDFium page. A regression is measurable and valid experimental evidence.
foreach(field IN ITEMS status default_changed pixel_parity_qualified
    repeat_equal source_alpha_changed_pixels
    changes_outside_horizontal_interior abs_rgb_error
    mean_abs_rgb_error changed_pixels gross_delta64_pixels
    max_channel_delta rgb_fnv64)
  string(JSON h_${field} GET "${json}" mode5_${field})
endforeach()
string(LENGTH "${h_rgb_fnv64}" h_fnv_len)
if(NOT h_status STREQUAL "measured_unqualified"
   OR h_default_changed OR h_pixel_parity_qualified OR NOT h_repeat_equal
   OR h_source_alpha_changed_pixels LESS 1
   OR h_source_alpha_changed_pixels GREATER 501832
   OR NOT h_changes_outside_horizontal_interior EQUAL 0
   OR NOT h_fnv_len EQUAL 16 OR NOT h_rgb_fnv64 MATCHES "^[0-9a-f]+$"
   OR h_abs_rgb_error LESS 0 OR h_abs_rgb_error GREATER 383901480
   OR h_mean_abs_rgb_error LESS 0 OR h_mean_abs_rgb_error GREATER 255
   OR h_changed_pixels LESS 0 OR h_changed_pixels GREATER 501832
   OR h_gross_delta64_pixels LESS 0
   OR h_gross_delta64_pixels GREATER h_changed_pixels
   OR h_max_channel_delta LESS 0 OR h_max_channel_delta GREATER 255)
  message(FATAL_ERROR "Private horizontal A/B status, budget or pixel scope invalid")
endif()
message(STATUS "jspdf-single-horizontal-analytic: mode4_rgb=${got_mode4_abs_rgb_error} mode5_rgb=${h_abs_rgb_error} mode4_changed=${got_mode4_changed_pixels} mode5_changed=${h_changed_pixels} source_changes=${h_source_alpha_changed_pixels} pixel_parity=false")


# Sixth explicit PRIVATE C11 one-rectangle W-clipped right-Fill border A/B.
# The x=150 original edge is the FILL's right bound, not W Clip's right.
# The new result may be worse; no invented PDFium-equivalence tolerance.
foreach(field IN ITEMS status default_changed pixel_parity_qualified
    repeat_equal source_alpha_changed_pixels
    changes_outside_fill_right_interior abs_rgb_error
    mean_abs_rgb_error changed_pixels gross_delta64_pixels
    max_channel_delta rgb_fnv64)
  string(JSON fillr_${field} GET "${json}" rect_fill_right_${field})
endforeach()
string(LENGTH "${fillr_rgb_fnv64}" fillr_fnv_len)
if(NOT fillr_status STREQUAL "measured_unqualified"
   OR fillr_default_changed OR fillr_pixel_parity_qualified
   OR NOT fillr_repeat_equal
   OR fillr_source_alpha_changed_pixels LESS 1
   OR fillr_source_alpha_changed_pixels GREATER 501832
   OR NOT fillr_changes_outside_fill_right_interior EQUAL 0
   OR NOT fillr_fnv_len EQUAL 16
   OR NOT fillr_rgb_fnv64 MATCHES "^[0-9a-f]+$"
   OR fillr_abs_rgb_error LESS 0 OR fillr_abs_rgb_error GREATER 383901480
   OR fillr_mean_abs_rgb_error LESS 0
   OR fillr_mean_abs_rgb_error GREATER 255
   OR fillr_changed_pixels LESS 0 OR fillr_changed_pixels GREATER 501832
   OR fillr_gross_delta64_pixels LESS 0
   OR fillr_gross_delta64_pixels GREATER fillr_changed_pixels
   OR fillr_max_channel_delta LESS 0 OR fillr_max_channel_delta GREATER 255)
  message(FATAL_ERROR "Original fill-right A/B scope, repeat or sample bounds failed")
endif()
# Existing exact green mode5 original A/B stays byte-identical.
if(NOT h_abs_rgb_error EQUAL 6863
   OR NOT h_changed_pixels EQUAL 595
   OR NOT h_gross_delta64_pixels EQUAL 0
   OR NOT h_max_channel_delta EQUAL 38
   OR NOT h_rgb_fnv64 STREQUAL "b6b1c313e4693ab8")
  message(FATAL_ERROR "Mode5 original Stroke reference changed under Fill trial")
endif()
message(STATUS "jspdf-fill-right-private: baseline_rgb=${h_abs_rgb_error} trial_rgb=${fillr_abs_rgb_error} changed=${fillr_changed_pixels} alpha_changes=${fillr_source_alpha_changed_pixels} parity=false official=0")

file(SHA256 "${SOURCE}" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "Original jsPDF source mutated by A/B")
endif()
message(STATUS
  "jspdf-single-vertical-analytic: status=measured_unqualified old_rgb=${got_mode3_abs_rgb_error} new_rgb=${got_mode4_abs_rgb_error} changed=${got_mode4_changed_pixels} gross=${got_mode4_gross_delta64_pixels} source_alpha_changes=${got_source_stroke_alpha_changed_pixels} official=0")
