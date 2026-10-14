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


# Seventh-stage read-only POST mode6 original-page residual accounting.
# Exactly 11 mutually exclusive source-anchored bins, including OTHER.
# No new renderer, tolerance, admission, SDK/backend or score promotion.
string(JSON last_schema GET "${json}" rect_fill_right_post_ab_residual_regions schema_version)
string(JSON last_obs GET "${json}" rect_fill_right_post_ab_residual_regions observation_only)
string(JSON last_parity GET "${json}" rect_fill_right_post_ab_residual_regions pixel_parity_qualified)
string(JSON last_ok GET "${json}" rect_fill_right_post_ab_residual_regions full_original_reconciled)
string(JSON last_regions_len LENGTH "${json}" rect_fill_right_post_ab_residual_regions regions)
string(JSON last_worst_len LENGTH "${json}" rect_fill_right_post_ab_residual_regions worst_pixels)
string(JSON last_witness_len LENGTH "${json}" rect_fill_right_post_ab_residual_regions witnesses)
if(NOT last_schema EQUAL 1 OR NOT last_obs OR last_parity OR
   NOT last_ok OR NOT last_regions_len EQUAL 11
   OR last_worst_len LESS 1 OR last_worst_len GREATER 12
   OR NOT last_witness_len EQUAL 12)
  message(FATAL_ERROR "Original private Fill-right residual audit not complete")
endif()
set(last_names bottom_single_horizontal_stroke right_vertical_stroke
    left_vertical_below_red top_repeated_stroke red_fill_right_boundary
    red_left_shared_stroke_clip red_left_clip_only red_top_stroke_shared
    red_fill_bottom_boundary red_bottom_uncovered_fringe other)
set(last_sum_pixels 0)
set(last_sum_changed 0)
set(last_sum_gross 0)
set(last_sum_error 0)
set(last_sum_lighter 0)
set(last_sum_darker 0)
set(last_max_delta 0)
foreach(name IN LISTS last_names)
  foreach(field IN ITEMS pixels changed gross abs_rgb_error
      brighter_rgb_error darker_rgb_error
      max_channel_delta worst_x worst_y)
    string(JSON src_${field} GET "${json}"
      rect_fill_right_post_ab_residual_regions regions ${name} ${field})
  endforeach()
  math(EXPR group_bias "${src_brighter_rgb_error}+${src_darker_rgb_error}")
  if(src_pixels LESS 0 OR src_pixels GREATER 501832
     OR src_changed LESS 0 OR src_changed GREATER src_pixels
     OR src_gross LESS 0 OR src_gross GREATER src_changed
     OR src_abs_rgb_error LESS 0 OR src_abs_rgb_error GREATER 383901480
     OR NOT group_bias EQUAL src_abs_rgb_error
     OR src_max_channel_delta LESS 0 OR src_max_channel_delta GREATER 255
     OR src_worst_x LESS 0 OR src_worst_x GREATER 595
     OR src_worst_y LESS 0 OR src_worst_y GREATER 841)
    message(FATAL_ERROR "Real original source-group residual out of range")
  endif()
  math(EXPR last_sum_pixels "${last_sum_pixels}+${src_pixels}")
  math(EXPR last_sum_changed "${last_sum_changed}+${src_changed}")
  math(EXPR last_sum_gross "${last_sum_gross}+${src_gross}")
  math(EXPR last_sum_error "${last_sum_error}+${src_abs_rgb_error}")
  math(EXPR last_sum_lighter "${last_sum_lighter}+${src_brighter_rgb_error}")
  math(EXPR last_sum_darker "${last_sum_darker}+${src_darker_rgb_error}")
  if(src_max_channel_delta GREATER last_max_delta)
    set(last_max_delta "${src_max_channel_delta}")
  endif()
endforeach()
math(EXPR last_bias_total "${last_sum_lighter}+${last_sum_darker}")
if(NOT last_sum_pixels EQUAL 501832
   OR NOT last_sum_changed EQUAL fillr_changed_pixels
   OR NOT last_sum_gross EQUAL fillr_gross_delta64_pixels
   OR NOT last_sum_error EQUAL fillr_abs_rgb_error
   OR NOT last_bias_total EQUAL fillr_abs_rgb_error
   OR NOT last_max_delta EQUAL fillr_max_channel_delta)
  message(FATAL_ERROR "Read-only residual groups contradict entire source page")
endif()
string(JSON last_leading GET "${json}"
    rect_fill_right_post_ab_residual_regions worst_pixels 0 max_channel_delta)
if(NOT last_leading EQUAL fillr_max_channel_delta)
  message(FATAL_ERROR "Leading original worst pixel contradicts whole image")
endif()
set(last_prior_delta 256)
math(EXPR last_worst_end "${last_worst_len}-1")
foreach(i RANGE 0 ${last_worst_end})
  foreach(field IN ITEMS x y region max_channel_delta
      rgb_error stroke_alpha fill_alpha)
    string(JSON last_w_${field} GET "${json}"
      rect_fill_right_post_ab_residual_regions worst_pixels ${i} ${field})
  endforeach()
  list(FIND last_names "${last_w_region}" last_region_index)
  string(JSON native_count LENGTH "${json}" rect_fill_right_post_ab_residual_regions worst_pixels ${i} native_rgb)
  string(JSON oracle_count LENGTH "${json}" rect_fill_right_post_ab_residual_regions worst_pixels ${i} pdfium_rgb)
  if(last_region_index LESS 0
     OR last_w_x LESS 0 OR last_w_x GREATER 595
     OR last_w_y LESS 0 OR last_w_y GREATER 841
     OR last_w_max_channel_delta LESS 1
     OR last_w_max_channel_delta GREATER last_prior_delta
     OR last_w_rgb_error LESS last_w_max_channel_delta
     OR last_w_rgb_error GREATER 765
     OR last_w_stroke_alpha LESS 0 OR last_w_stroke_alpha GREATER 255
     OR last_w_fill_alpha LESS 0 OR last_w_fill_alpha GREATER 255
     OR NOT native_count EQUAL 3 OR NOT oracle_count EQUAL 3)
    message(FATAL_ERROR "Malformed original-pixel worst-site evidence")
  endif()
  set(last_prior_delta "${last_w_max_channel_delta}")
endforeach()
set(last_witness_names bottom_left bottom_center bottom_right
    right_vertical left_vertical_lower left_red_stroke_overlap
    left_outside top_corner red_top red_bottom red_right
    red_bottom_uncovered)
set(last_witness_x 50 100 250 250 50 50 49 250 100 100 150 100)
set(last_witness_y 139 139 139 60 110 40 40 19 20 99 40 100)
foreach(i RANGE 0 11)
  list(GET last_witness_names ${i} expected_name)
  list(GET last_witness_x ${i} expected_x)
  list(GET last_witness_y ${i} expected_y)
  foreach(field IN ITEMS name region x y stroke_alpha fill_alpha)
    string(JSON wit_${field} GET "${json}"
      rect_fill_right_post_ab_residual_regions witnesses ${i} ${field})
  endforeach()
  list(FIND last_names "${wit_region}" witness_region_index)
  string(JSON native_count LENGTH "${json}" rect_fill_right_post_ab_residual_regions witnesses ${i} native_rgb)
  string(JSON oracle_count LENGTH "${json}" rect_fill_right_post_ab_residual_regions witnesses ${i} pdfium_rgb)
  if(NOT wit_name STREQUAL expected_name
     OR NOT wit_x EQUAL expected_x OR NOT wit_y EQUAL expected_y
     OR witness_region_index LESS 0
     OR wit_stroke_alpha LESS 0 OR wit_stroke_alpha GREATER 255
     OR wit_fill_alpha LESS 0 OR wit_fill_alpha GREATER 255
     OR NOT native_count EQUAL 3 OR NOT oracle_count EQUAL 3)
    message(FATAL_ERROR "Original jsPDF source witness missing/invalid")
  endif()
  foreach(channel RANGE 0 2)
    string(JSON actual GET "${json}" rect_fill_right_post_ab_residual_regions witnesses ${i} native_rgb ${channel})
    string(JSON truth GET "${json}" rect_fill_right_post_ab_residual_regions witnesses ${i} pdfium_rgb ${channel})
    if(actual LESS 0 OR actual GREATER 255
       OR truth LESS 0 OR truth GREATER 255)
      message(FATAL_ERROR "Invalid original RGB witness channel")
    endif()
  endforeach()
endforeach()
# This branch is strictly observation-only. Freeze exact last accepted A/B.
if(NOT fillr_abs_rgb_error EQUAL 4019
   OR NOT fillr_changed_pixels EQUAL 516
   OR NOT fillr_gross_delta64_pixels EQUAL 0
   OR NOT fillr_max_channel_delta EQUAL 38
   OR NOT fillr_rgb_fnv64 STREQUAL "4d2bfdd4bbf32298")
  message(FATAL_ERROR "Last qualified Fill-right mode output mutated")
endif()


# Seventh PRIVATE original-source A/B for ONE strictly admitted W Clip LEFT
# boundary. May regress; reporting is NOT original/native certification.
foreach(field IN ITEMS status default_changed pixel_parity_qualified
    repeat_equal source_alpha_changed_pixels
    changes_outside_w_clip_left_interior abs_rgb_error
    mean_abs_rgb_error changed_pixels gross_delta64_pixels
    max_channel_delta rgb_fnv64)
  string(JSON left_${field} GET "${json}" rect_clip_left_${field})
endforeach()
string(LENGTH "${left_rgb_fnv64}" left_digest_length)
if(NOT left_status STREQUAL "measured_unqualified"
   OR left_default_changed OR left_pixel_parity_qualified
   OR NOT left_repeat_equal
   OR left_source_alpha_changed_pixels LESS 1
   OR left_source_alpha_changed_pixels GREATER 79
   OR NOT left_changes_outside_w_clip_left_interior EQUAL 0
   OR NOT left_digest_length EQUAL 16
   OR NOT left_rgb_fnv64 MATCHES "^[0-9a-f]+$"
   OR left_abs_rgb_error LESS 0 OR left_abs_rgb_error GREATER 383901480
   OR left_mean_abs_rgb_error LESS 0
   OR left_mean_abs_rgb_error GREATER 255
   OR left_changed_pixels LESS 0 OR left_changed_pixels GREATER 501832
   OR left_gross_delta64_pixels LESS 0
   OR left_gross_delta64_pixels GREATER left_changed_pixels
   OR left_max_channel_delta LESS 0 OR left_max_channel_delta GREATER 255)
  message(FATAL_ERROR "Unqualified Clip-left A/B changed outside scope or budget")
endif()
# Original preceding candidate and its complete source-effect breakdown
# remain byte-identical; no hidden promotion of whole-original admission.
if(NOT fillr_abs_rgb_error EQUAL 4019
   OR NOT fillr_changed_pixels EQUAL 516
   OR NOT fillr_gross_delta64_pixels EQUAL 0
   OR NOT fillr_max_channel_delta EQUAL 38
   OR NOT fillr_rgb_fnv64 STREQUAL "4d2bfdd4bbf32298")
  message(FATAL_ERROR "CI-green private Fill-right original baseline changed")
endif()
message(STATUS
 "jspdf-w-clip-left-private: baseline_rgb=${fillr_abs_rgb_error} trial_rgb=${left_abs_rgb_error} changed=${left_changed_pixels} alpha_changes=${left_source_alpha_changed_pixels} official=0 pixel_parity=false")


# Eighth, strictly READ-ONLY residual audit after private Clip-left mode7.
# All 501,832 untouched source-page pixels must be allocated to exactly
# one of 16 source-labelled buckets; no new renderer or parity promotion.
string(JSON m7_schema GET "${json}" rect_clip_left_original_residual_regions schema_version)
string(JSON m7_obs GET "${json}" rect_clip_left_original_residual_regions observation_only)
string(JSON m7_parity GET "${json}" rect_clip_left_original_residual_regions pixel_parity_qualified)
string(JSON m7_reconciled GET "${json}" rect_clip_left_original_residual_regions full_original_reconciled)
string(JSON m7_count LENGTH "${json}" rect_clip_left_original_residual_regions regions)
string(JSON m7_worst_count LENGTH "${json}" rect_clip_left_original_residual_regions worst_pixels)
string(JSON m7_witness_count LENGTH "${json}" rect_clip_left_original_residual_regions witnesses)
if(NOT m7_schema EQUAL 1 OR NOT m7_obs OR m7_parity
   OR NOT m7_reconciled OR NOT m7_count EQUAL 16
   OR m7_worst_count LESS 1 OR m7_worst_count GREATER 16
   OR NOT m7_witness_count EQUAL 16)
  message(FATAL_ERROR "Missing or invalid private mode7 original residual audit")
endif()
set(m7_regions bottom_left_endpoint bottom_right_endpoint
    bottom_single_horizontal_interior
    top_repeated_left_endpoint top_repeated_right_endpoint
    top_repeated_horizontal_interior right_vertical_stroke
    left_vertical_below_red red_left_w_clip_stroke_overlap
    red_left_w_clip_only red_right_fill_boundary
    red_top_right_coupled_corner red_top_stroke_overlap
    red_fill_bottom_edge red_bottom_uncovered_fringe other)
set(m7_total_pixels 0)
set(m7_total_changed 0)
set(m7_total_gross 0)
set(m7_total_rgb 0)
set(m7_total_brighter 0)
set(m7_total_darker 0)
set(m7_overall_max 0)
foreach(region IN LISTS m7_regions)
  foreach(field IN ITEMS pixels changed gross abs_rgb_error
      brighter_rgb_error darker_rgb_error
      max_channel_delta worst_x worst_y)
    string(JSON m7_${field} GET "${json}"
      rect_clip_left_original_residual_regions regions ${region} ${field})
  endforeach()
  math(EXPR m7_signed "${m7_brighter_rgb_error}+${m7_darker_rgb_error}")
  if(m7_pixels LESS 0 OR m7_pixels GREATER 501832
     OR m7_changed LESS 0 OR m7_changed GREATER m7_pixels
     OR m7_gross LESS 0 OR m7_gross GREATER m7_changed
     OR m7_abs_rgb_error LESS 0 OR m7_abs_rgb_error GREATER 383901480
     OR NOT m7_signed EQUAL m7_abs_rgb_error
     OR m7_max_channel_delta LESS 0 OR m7_max_channel_delta GREATER 255
     OR m7_worst_x LESS 0 OR m7_worst_x GREATER 595
     OR m7_worst_y LESS 0 OR m7_worst_y GREATER 841)
    message(FATAL_ERROR "Source-original mode7 region counter invalid")
  endif()
  math(EXPR m7_total_pixels "${m7_total_pixels}+${m7_pixels}")
  math(EXPR m7_total_changed "${m7_total_changed}+${m7_changed}")
  math(EXPR m7_total_gross "${m7_total_gross}+${m7_gross}")
  math(EXPR m7_total_rgb "${m7_total_rgb}+${m7_abs_rgb_error}")
  math(EXPR m7_total_brighter "${m7_total_brighter}+${m7_brighter_rgb_error}")
  math(EXPR m7_total_darker "${m7_total_darker}+${m7_darker_rgb_error}")
  if(m7_max_channel_delta GREATER m7_overall_max)
    set(m7_overall_max "${m7_max_channel_delta}")
  endif()
endforeach()
math(EXPR m7_signed_total "${m7_total_brighter}+${m7_total_darker}")
if(NOT m7_total_pixels EQUAL 501832
   OR NOT m7_total_changed EQUAL left_changed_pixels
   OR NOT m7_total_gross EQUAL left_gross_delta64_pixels
   OR NOT m7_total_rgb EQUAL left_abs_rgb_error
   OR NOT m7_signed_total EQUAL left_abs_rgb_error
   OR NOT m7_overall_max EQUAL left_max_channel_delta)
  message(FATAL_ERROR "Original mode7 residual bins do not reconcile")
endif()
string(JSON m7_top_delta GET "${json}"
    rect_clip_left_original_residual_regions worst_pixels 0 max_channel_delta)
if(NOT m7_top_delta EQUAL left_max_channel_delta)
  message(FATAL_ERROR "Original mode7 worst pixel does not match global max")
endif()
set(m7_prev_delta 256)
math(EXPR m7_last_worst "${m7_worst_count}-1")
foreach(i RANGE 0 ${m7_last_worst})
  foreach(field IN ITEMS x y region max_channel_delta rgb_error
      stroke_alpha fill_alpha)
    string(JSON m7_worst_${field} GET "${json}"
      rect_clip_left_original_residual_regions worst_pixels ${i} ${field})
  endforeach()
  list(FIND m7_regions "${m7_worst_region}" m7_region_id)
  string(JSON m7_nat_count LENGTH "${json}"
    rect_clip_left_original_residual_regions worst_pixels ${i} native_rgb)
  string(JSON m7_pdf_count LENGTH "${json}"
    rect_clip_left_original_residual_regions worst_pixels ${i} pdfium_rgb)
  if(m7_region_id LESS 0
     OR m7_worst_x LESS 0 OR m7_worst_x GREATER 595
     OR m7_worst_y LESS 0 OR m7_worst_y GREATER 841
     OR m7_worst_max_channel_delta LESS 1
     OR m7_worst_max_channel_delta GREATER m7_prev_delta
     OR m7_worst_rgb_error LESS m7_worst_max_channel_delta
     OR m7_worst_rgb_error GREATER 765
     OR m7_worst_stroke_alpha LESS 0 OR m7_worst_stroke_alpha GREATER 255
     OR m7_worst_fill_alpha LESS 0 OR m7_worst_fill_alpha GREATER 255
     OR NOT m7_nat_count EQUAL 3 OR NOT m7_pdf_count EQUAL 3)
    message(FATAL_ERROR "Bad original-PDF mode7 worst-pixel data")
  endif()
  set(m7_prev_delta "${m7_worst_max_channel_delta}")
endforeach()
set(m7_expected_names bottom_outer_left bottom_left_join
    bottom_interior bottom_right_join bottom_right_upper
    top_outer_left top_right_corner right_vertical
    left_vertical_lower red_left_overlap left_outer_stroke
    red_right red_top_right_corner red_bottom_interior
    red_bottom_fringe red_top_interior)
set(m7_expected_xs 49 50 100 250 250 49 250 250
    50 50 49 150 150 100 100 100)
set(m7_expected_ys 139 139 139 139 138 20 19 60
    110 40 40 40 20 99 100 20)
foreach(i RANGE 0 15)
  list(GET m7_expected_names ${i} m7_expected_name)
  list(GET m7_expected_xs ${i} m7_expected_x)
  list(GET m7_expected_ys ${i} m7_expected_y)
  foreach(field IN ITEMS name x y region stroke_alpha fill_alpha)
    string(JSON m7_wit_${field} GET "${json}"
      rect_clip_left_original_residual_regions witnesses ${i} ${field})
  endforeach()
  list(FIND m7_regions "${m7_wit_region}" m7_wit_region_id)
  string(JSON m7_nat_len LENGTH "${json}"
    rect_clip_left_original_residual_regions witnesses ${i} native_rgb)
  string(JSON m7_pdf_len LENGTH "${json}"
    rect_clip_left_original_residual_regions witnesses ${i} pdfium_rgb)
  if(NOT m7_wit_name STREQUAL m7_expected_name
     OR NOT m7_wit_x EQUAL m7_expected_x
     OR NOT m7_wit_y EQUAL m7_expected_y
     OR m7_wit_region_id LESS 0
     OR m7_wit_stroke_alpha LESS 0 OR m7_wit_stroke_alpha GREATER 255
     OR m7_wit_fill_alpha LESS 0 OR m7_wit_fill_alpha GREATER 255
     OR NOT m7_nat_len EQUAL 3 OR NOT m7_pdf_len EQUAL 3)
    message(FATAL_ERROR "Original mode7 witness coordinates invalid")
  endif()
  foreach(ch RANGE 0 2)
    string(JSON m7_value GET "${json}"
      rect_clip_left_original_residual_regions witnesses ${i} native_rgb ${ch})
    string(JSON m7_truth GET "${json}"
      rect_clip_left_original_residual_regions witnesses ${i} pdfium_rgb ${ch})
    if(m7_value LESS 0 OR m7_value GREATER 255
       OR m7_truth LESS 0 OR m7_truth GREATER 255)
      message(FATAL_ERROR "Out-of-range original mode7 RGB witness")
    endif()
  endforeach()
endforeach()
# Read-only changes cannot alter the already CI-qualified original
# mode7 whole-page observations, nor enable a public positive.
if(NOT left_abs_rgb_error EQUAL 2519
   OR NOT left_changed_pixels EQUAL 516
   OR NOT left_gross_delta64_pixels EQUAL 0
   OR NOT left_max_channel_delta EQUAL 38
   OR NOT left_rgb_fnv64 STREQUAL "37927e64c5916882")
  message(FATAL_ERROR "CI-green original mode7 output changed")
endif()
message(STATUS
  "jspdf-mode7-original-residual: reconciled=501832 pixels changed=${m7_total_changed} error=${m7_total_rgb} gross=${m7_total_gross} parity=false official=0")


# Eighth EXPLICIT PRIVATE source-derived red Fill bottom-fringe A/B.
# Its new source RGBA result must be deterministic, isolated to the one
# previously unpainted device row and separately sample-budgeted.
# A regression is valid evidence: do NOT invent a parity threshold.
foreach(field IN ITEMS status default_changed pixel_parity_qualified
    repeat_equal source_rgba_changed_pixels
    changes_outside_bottom_interior abs_rgb_error
    mean_abs_rgb_error changed_pixels gross_delta64_pixels
    max_channel_delta rgb_fnv64)
  string(JSON bottom_${field} GET "${json}" rect_fill_bottom_${field})
endforeach()
string(LENGTH "${bottom_rgb_fnv64}" bottom_digest_length)
if(NOT bottom_status STREQUAL "measured_unqualified"
   OR bottom_default_changed OR bottom_pixel_parity_qualified
   OR NOT bottom_repeat_equal
   OR bottom_source_rgba_changed_pixels LESS 1
   OR bottom_source_rgba_changed_pixels GREATER 97
   OR NOT bottom_changes_outside_bottom_interior EQUAL 0
   OR NOT bottom_digest_length EQUAL 16
   OR NOT bottom_rgb_fnv64 MATCHES "^[0-9a-f]+$"
   OR bottom_abs_rgb_error LESS 0 OR bottom_abs_rgb_error GREATER 383901480
   OR bottom_mean_abs_rgb_error LESS 0
   OR bottom_mean_abs_rgb_error GREATER 255
   OR bottom_changed_pixels LESS 0 OR bottom_changed_pixels GREATER 501832
   OR bottom_gross_delta64_pixels LESS 0
   OR bottom_gross_delta64_pixels GREATER bottom_changed_pixels
   OR bottom_max_channel_delta LESS 0
   OR bottom_max_channel_delta GREATER 255)
  message(FATAL_ERROR "Original Fill-bottom A/B scope, repeat or budget invalid")
endif()
# Read-only mode7 source attribution and exact already-CI-green bytes
# MUST NOT silently shift when the eighth test-only variant runs.
if(NOT left_abs_rgb_error EQUAL 2519
   OR NOT left_changed_pixels EQUAL 516
   OR NOT left_gross_delta64_pixels EQUAL 0
   OR NOT left_max_channel_delta EQUAL 38
   OR NOT left_rgb_fnv64 STREQUAL "37927e64c5916882")
  message(FATAL_ERROR "Previously qualified Clip-left source image changed")
endif()
message(STATUS
  "jspdf-private-fill-bottom: mode7_rgb=${left_abs_rgb_error} bottom_rgb=${bottom_abs_rgb_error} mode7_changed=${left_changed_pixels} bottom_changed=${bottom_changed_pixels} changed_rgba=${bottom_source_rgba_changed_pixels} parity=false official=0")


# Ninth independent original-PDF A/B: sixth explicitly callable C11
# STROKE mode only; inherits exact mode5 single-pass horizontal source
# geometry eligibility and compares CEIL to the prior rounded alpha.
# No improvement assumption, tolerance or native-positive promotion.
foreach(field IN ITEMS status default_changed pixel_parity_qualified
    repeat_equal source_alpha_changed_pixels
    outside_single_horizontal_interior alpha_delta_gt1_pixels
    abs_rgb_error mean_abs_rgb_error changed_pixels
    gross_delta64_pixels max_channel_delta rgb_fnv64)
  string(JSON ceil_${field} GET "${json}" stroke_ceil_${field})
endforeach()
string(LENGTH "${ceil_rgb_fnv64}" ceil_fnv_len)
if(NOT ceil_status STREQUAL "measured_unqualified"
   OR ceil_default_changed OR ceil_pixel_parity_qualified
   OR NOT ceil_repeat_equal
   OR ceil_source_alpha_changed_pixels LESS 1
   OR ceil_source_alpha_changed_pixels GREATER 591
   OR NOT ceil_outside_single_horizontal_interior EQUAL 0
   OR NOT ceil_alpha_delta_gt1_pixels EQUAL 0
   OR NOT ceil_fnv_len EQUAL 16
   OR NOT ceil_rgb_fnv64 MATCHES "^[0-9a-f]+$"
   OR ceil_abs_rgb_error LESS 0 OR ceil_abs_rgb_error GREATER 383901480
   OR ceil_mean_abs_rgb_error LESS 0
   OR ceil_mean_abs_rgb_error GREATER 255
   OR ceil_changed_pixels LESS 0 OR ceil_changed_pixels GREATER 501832
   OR ceil_gross_delta64_pixels LESS 0
   OR ceil_gross_delta64_pixels GREATER ceil_changed_pixels
   OR ceil_max_channel_delta LESS 0
   OR ceil_max_channel_delta GREATER 255)
  message(FATAL_ERROR "Private source-horizontal CEIL A/B out of isolation scope")
endif()
# Known-green original #296 composite/source identity is immutable on
# this Stroke-only branch. This is not a new source-positive acceptance.
if(NOT bottom_abs_rgb_error EQUAL 1937
   OR NOT bottom_changed_pixels EQUAL 419
   OR NOT bottom_gross_delta64_pixels EQUAL 0
   OR NOT bottom_max_channel_delta EQUAL 38
   OR NOT bottom_rgb_fnv64 STREQUAL "7a88c595fcde2208")
  message(FATAL_ERROR "Qualified source Fill-bottom mode8 baseline changed")
endif()
message(STATUS
  "jspdf-private-horizontal-ceil: baseline_rgb=${bottom_abs_rgb_error} trial_rgb=${ceil_abs_rgb_error} baseline_changed=${bottom_changed_pixels} trial_changed=${ceil_changed_pixels} source_alpha_changes=${ceil_source_alpha_changed_pixels} official=0 parity=false")


# TENTH independently callable original-PDF Stroke quantization trial:
# retain single-pass source geometry eligibility but add one alpha unit
# ONLY when the whole device row is above the source horizontal center.
# No improvement threshold, numerical oracle fallback or positive count.
foreach(field IN ITEMS status default_changed pixel_parity_qualified
    repeat_equal source_alpha_changed_pixels
    outside_top_horizontal_interior alpha_delta_gt1_pixels
    abs_rgb_error mean_abs_rgb_error changed_pixels
    gross_delta64_pixels max_channel_delta rgb_fnv64)
  string(JSON upper_${field} GET "${json}" stroke_upper_${field})
endforeach()
string(LENGTH "${upper_rgb_fnv64}" upper_hashlen)
if(NOT upper_status STREQUAL "measured_unqualified"
   OR upper_default_changed OR upper_pixel_parity_qualified
   OR NOT upper_repeat_equal
   OR upper_source_alpha_changed_pixels LESS 1
   OR upper_source_alpha_changed_pixels GREATER 394
   OR NOT upper_outside_top_horizontal_interior EQUAL 0
   OR NOT upper_alpha_delta_gt1_pixels EQUAL 0
   OR NOT upper_hashlen EQUAL 16
   OR NOT upper_rgb_fnv64 MATCHES "^[0-9a-f]+$"
   OR upper_abs_rgb_error LESS 0 OR upper_abs_rgb_error GREATER 383901480
   OR upper_mean_abs_rgb_error LESS 0
   OR upper_mean_abs_rgb_error GREATER 255
   OR upper_changed_pixels LESS 0 OR upper_changed_pixels GREATER 501832
   OR upper_gross_delta64_pixels LESS 0
   OR upper_gross_delta64_pixels GREATER upper_changed_pixels
   OR upper_max_channel_delta LESS 0
   OR upper_max_channel_delta GREATER 255)
  message(FATAL_ERROR "Unqualified single-horizontal TOP side A/B report")
endif()
# The earlier global CEIL is frozen as independent negative evidence;
# do NOT conflate it with the new upper-side experiment or change the
# previous CI-green clipped Fill-bottom composite.
if(NOT ceil_abs_rgb_error EQUAL 2525
   OR NOT ceil_changed_pixels EQUAL 615
   OR NOT ceil_gross_delta64_pixels EQUAL 0
   OR NOT ceil_max_channel_delta EQUAL 38
   OR NOT ceil_rgb_fnv64 STREQUAL "d2d04c526c2c20dc")
  message(FATAL_ERROR "Previously measured negative global CEIL A/B mutated")
endif()
if(NOT bottom_abs_rgb_error EQUAL 1937
   OR NOT bottom_changed_pixels EQUAL 419
   OR NOT bottom_gross_delta64_pixels EQUAL 0
   OR NOT bottom_max_channel_delta EQUAL 38
   OR NOT bottom_rgb_fnv64 STREQUAL "7a88c595fcde2208")
  message(FATAL_ERROR "Qualified mode8 original-Fill reference changed")
endif()
message(STATUS
  "jspdf-upper-horizontal-lsb: baseline_rgb=${bottom_abs_rgb_error} global_ceil_rgb=${ceil_abs_rgb_error} upper_rgb=${upper_abs_rgb_error} upper_changed=${upper_changed_pixels} alpha_changed=${upper_source_alpha_changed_pixels} official=0 parity=false")

# ELEVENTH independent private A/B: EXACT accepted #298 Stroke and #296
# clipped-fill RGBA bytes; destination RGB is composed onto opaque white.
# Earlier modes/SDK/parity count remain unchanged.
if(NOT upper_abs_rgb_error EQUAL 1349
   OR NOT upper_changed_pixels EQUAL 223
   OR NOT upper_gross_delta64_pixels EQUAL 0
   OR NOT upper_max_channel_delta EQUAL 38
   OR NOT upper_rgb_fnv64 STREQUAL "04d883c87f2ec8bc")
  message(FATAL_ERROR "Previously measured #298 source image changed")
endif()
foreach(field IN ITEMS status default_changed pixel_parity_qualified
    repeat_equal source_layers_equal
    changed_vs_upper_pixels changed_outside_overlap
    abs_rgb_error mean_abs_rgb_error changed_pixels
    gross_delta64_pixels max_channel_delta rgb_fnv64)
  string(JSON opaque_${field} GET "${json}" opaque_floor_${field})
endforeach()
string(LENGTH "${opaque_rgb_fnv64}" opaque_hashlen)
if(NOT opaque_status STREQUAL "measured_unqualified"
   OR opaque_default_changed OR opaque_pixel_parity_qualified
   OR NOT opaque_repeat_equal OR NOT opaque_source_layers_equal
   OR opaque_changed_vs_upper_pixels LESS 1
   OR opaque_changed_vs_upper_pixels GREATER 501832
   OR NOT opaque_changed_outside_overlap EQUAL 0
   OR opaque_abs_rgb_error LESS 0 OR opaque_abs_rgb_error GREATER 383901480
   OR opaque_mean_abs_rgb_error LESS 0 OR opaque_mean_abs_rgb_error GREATER 255
   OR opaque_changed_pixels LESS 0 OR opaque_changed_pixels GREATER 501832
   OR opaque_gross_delta64_pixels LESS 0
   OR opaque_gross_delta64_pixels GREATER opaque_changed_pixels
   OR opaque_max_channel_delta LESS 0 OR opaque_max_channel_delta GREATER 255
   OR NOT opaque_hashlen EQUAL 16
   OR NOT opaque_rgb_fnv64 MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Opaque-white floor A/B escaped isolation")
endif()
message(STATUS
  "jspdf-opaque-white-floor: upper_rgb=${upper_abs_rgb_error} trial_rgb=${opaque_abs_rgb_error} upper_changed=${upper_changed_pixels} trial_changed=${opaque_changed_pixels} changed_vs_upper=${opaque_changed_vs_upper_pixels} outside_overlap=${opaque_changed_outside_overlap} official=0 parity=false")
# TWELFTH independent original-PDF device-Q8 A/B. Pin the completed
# Linux/macOS/Windows #302 opaque compositing result separately.
if(NOT opaque_abs_rgb_error EQUAL 1193
   OR NOT opaque_changed_pixels EQUAL 145
   OR NOT opaque_gross_delta64_pixels EQUAL 0
   OR NOT opaque_max_channel_delta EQUAL 38
   OR NOT opaque_changed_vs_upper_pixels EQUAL 78
   OR NOT opaque_changed_outside_overlap EQUAL 0
   OR NOT opaque_rgb_fnv64 STREQUAL "7932506afd903c78")
  message(FATAL_ERROR "Qualified private opaque-white composite control changed")
endif()
foreach(field IN ITEMS status default_changed pixel_parity_qualified
    repeat_equal source_alpha_changed_pixels outside_eligible_geometry
    alpha_delta_gt3_pixels abs_rgb_error mean_abs_rgb_error changed_pixels
    gross_delta64_pixels max_channel_delta rgb_fnv64)
  string(JSON q8_${field} GET "${json}" stroke_q8_${field})
endforeach()
foreach(field IN ITEMS status default_changed pixel_parity_qualified
    repeat_equal outside_source_alpha_change abs_rgb_error
    mean_abs_rgb_error changed_pixels gross_delta64_pixels
    max_channel_delta rgb_fnv64)
  string(JSON q8floor_${field} GET "${json}" q8_floor_${field})
endforeach()
string(LENGTH "${q8_rgb_fnv64}" q8_hashlen)
string(LENGTH "${q8floor_rgb_fnv64}" q8floor_hashlen)
if(NOT q8_status STREQUAL "measured_unqualified"
   OR q8_default_changed OR q8_pixel_parity_qualified OR NOT q8_repeat_equal
   OR q8_source_alpha_changed_pixels LESS 1
   OR q8_source_alpha_changed_pixels GREATER 501832
   OR NOT q8_outside_eligible_geometry EQUAL 0
   OR NOT q8_alpha_delta_gt3_pixels EQUAL 0
   OR q8_abs_rgb_error LESS 0 OR q8_abs_rgb_error GREATER 383901480
   OR q8_mean_abs_rgb_error LESS 0 OR q8_mean_abs_rgb_error GREATER 255
   OR q8_changed_pixels LESS 0 OR q8_changed_pixels GREATER 501832
   OR q8_gross_delta64_pixels LESS 0 OR q8_gross_delta64_pixels GREATER q8_changed_pixels
   OR q8_max_channel_delta LESS 0 OR q8_max_channel_delta GREATER 255
   OR NOT q8_hashlen EQUAL 16 OR NOT q8_rgb_fnv64 MATCHES "^[0-9a-f]+$"
   OR NOT q8floor_status STREQUAL "measured_unqualified"
   OR q8floor_default_changed OR q8floor_pixel_parity_qualified
   OR NOT q8floor_repeat_equal OR NOT q8floor_outside_source_alpha_change EQUAL 0
   OR q8floor_abs_rgb_error LESS 0 OR q8floor_abs_rgb_error GREATER 383901480
   OR q8floor_mean_abs_rgb_error LESS 0 OR q8floor_mean_abs_rgb_error GREATER 255
   OR q8floor_changed_pixels LESS 0 OR q8floor_changed_pixels GREATER 501832
   OR q8floor_gross_delta64_pixels LESS 0
   OR q8floor_gross_delta64_pixels GREATER q8floor_changed_pixels
   OR q8floor_max_channel_delta LESS 0 OR q8floor_max_channel_delta GREATER 255
   OR NOT q8floor_hashlen EQUAL 16
   OR NOT q8floor_rgb_fnv64 MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Unqualified Q8 device-edge source A/B escaped isolation")
endif()
message(STATUS
  "jspdf-device-Q8: old_rgba=${upper_abs_rgb_error} q8_rgba=${q8_abs_rgb_error} old_opaque=${opaque_abs_rgb_error} q8_opaque=${q8floor_abs_rgb_error} alpha_changed=${q8_source_alpha_changed_pixels} official=0 parity=false")
# Read-only successor: account for all remaining #303 Q8+opaque original
# pixels in the historical 16 exclusive diagnostic groups. No source
# geometry rule or admitted renderer code changes on this child branch.
if(NOT q8floor_abs_rgb_error EQUAL 845
   OR NOT q8floor_changed_pixels EQUAL 29
   OR NOT q8floor_gross_delta64_pixels EQUAL 0
   OR NOT q8floor_max_channel_delta EQUAL 38
   OR NOT q8floor_rgb_fnv64 STREQUAL "6ed067c204238cf8")
  message(FATAL_ERROR "Q8 parent original-pixel evidence mutated")
endif()
string(JSON audit_bins LENGTH "${json}" q8_floor_residual_regions regions)
string(JSON audit_pixels GET "${json}" q8_floor_residual_regions total_pixels)
string(JSON audit_changed GET "${json}" q8_floor_residual_regions total_changed)
string(JSON audit_rgb GET "${json}" q8_floor_residual_regions total_abs_rgb_error)
string(JSON audit_gross GET "${json}" q8_floor_residual_regions total_gross)
string(JSON audit_max GET "${json}" q8_floor_residual_regions max_channel_delta)
string(JSON audit_repeat GET "${json}" q8_floor_residual_regions full_original_reconciled)
string(JSON audit_parity GET "${json}" q8_floor_residual_regions pixel_parity_qualified)
string(JSON audit_witnesses LENGTH "${json}" q8_floor_residual_regions worst_pixels)
if(NOT audit_bins EQUAL 16 OR NOT audit_pixels EQUAL 501832
   OR NOT audit_changed EQUAL q8floor_changed_pixels
   OR NOT audit_rgb EQUAL q8floor_abs_rgb_error
   OR NOT audit_gross EQUAL q8floor_gross_delta64_pixels
   OR NOT audit_max EQUAL q8floor_max_channel_delta
   OR NOT audit_repeat OR audit_parity
   OR NOT audit_witnesses EQUAL 16)
  message(FATAL_ERROR "Q8 post-candidate region audit does not reconcile")
endif()
set(audit_changed_sum 0)
set(audit_rgb_sum 0)
foreach(bin IN ITEMS bottom_left_endpoint bottom_right_endpoint
    bottom_single_horizontal_interior top_repeated_left_endpoint
    top_repeated_right_endpoint top_repeated_horizontal_interior
    right_vertical_stroke left_vertical_below_red red_left_w_clip_stroke_overlap
    red_left_w_clip_only red_right_fill_boundary red_top_right_coupled_corner
    red_top_stroke_overlap red_fill_bottom_edge red_bottom_uncovered_fringe other)
  string(JSON bin_changed GET "${json}" q8_floor_residual_regions regions "${bin}" changed)
  string(JSON bin_error GET "${json}" q8_floor_residual_regions regions "${bin}" abs_rgb_error)
  math(EXPR audit_changed_sum "${audit_changed_sum}+${bin_changed}")
  math(EXPR audit_rgb_sum "${audit_rgb_sum}+${bin_error}")
endforeach()
if(NOT audit_changed_sum EQUAL audit_changed
   OR NOT audit_rgb_sum EQUAL audit_rgb)
  message(FATAL_ERROR "Q8 source-residual 16 bins do not sum to whole page")
endif()
message(STATUS
  "jspdf-q8-final-regions: mismatch=${audit_changed} error=${audit_rgb} gross=${audit_gross} max=${audit_max} bins=${audit_bins} parity=false")
# THIRTEENTH source-geometry, provider-free C11 A/B: orthogonal inner
# miter bevel contributes bounded signed coverage in Q8 device cells.
# Parent #303 Q8 and #304 all-pixel residual digest stay fixed.
if(NOT q8floor_abs_rgb_error EQUAL 845
   OR NOT q8floor_changed_pixels EQUAL 29
   OR NOT q8floor_gross_delta64_pixels EQUAL 0
   OR NOT q8floor_max_channel_delta EQUAL 38
   OR NOT q8floor_rgb_fnv64 STREQUAL "6ed067c204238cf8")
  message(FATAL_ERROR "Parent Q8 source-image baseline changed")
endif()
foreach(field IN ITEMS status default_changed pixel_parity_qualified
    repeat_equal source_alpha_changed_pixels outside_source_endpoint
    alpha_delta_gt64_pixels composite_outside_source_alpha
    rgba_abs_rgb_error rgba_changed_pixels rgba_fnv64
    abs_rgb_error mean_abs_rgb_error changed_pixels
    gross_delta64_pixels max_channel_delta rgb_fnv64)
  string(JSON join_${field} GET "${json}" miter_join_${field})
endforeach()
string(LENGTH "${join_rgb_fnv64}" join_hashlen)
string(LENGTH "${join_rgba_fnv64}" join_rgba_hashlen)
if(NOT join_status STREQUAL "measured_unqualified"
   OR join_default_changed OR join_pixel_parity_qualified
   OR NOT join_repeat_equal
   OR join_source_alpha_changed_pixels LESS 1
   OR join_source_alpha_changed_pixels GREATER 48
   OR NOT join_outside_source_endpoint EQUAL 0
   OR NOT join_alpha_delta_gt64_pixels EQUAL 0
   OR NOT join_composite_outside_source_alpha EQUAL 0
   OR join_rgba_abs_rgb_error LESS 0
   OR join_rgba_abs_rgb_error GREATER 383901480
   OR join_rgba_changed_pixels LESS 0
   OR join_rgba_changed_pixels GREATER 501832
   OR NOT join_rgba_hashlen EQUAL 16
   OR NOT join_rgba_fnv64 MATCHES "^[0-9a-f]+$"
   OR join_abs_rgb_error LESS 0 OR join_abs_rgb_error GREATER 383901480
   OR join_mean_abs_rgb_error LESS 0 OR join_mean_abs_rgb_error GREATER 255
   OR join_changed_pixels LESS 0 OR join_changed_pixels GREATER 501832
   OR join_gross_delta64_pixels LESS 0
   OR join_gross_delta64_pixels GREATER join_changed_pixels
   OR join_max_channel_delta LESS 0 OR join_max_channel_delta GREATER 255
   OR NOT join_hashlen EQUAL 16
   OR NOT join_rgb_fnv64 MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Q8 inner-bevel isolated join A/B escaped scope")
endif()
message(STATUS
  "jspdf-q8-inner-bevel-join: parent_rgb=${q8floor_abs_rgb_error} trial_rgb=${join_abs_rgb_error} parent_mismatch=${q8floor_changed_pixels} trial_mismatch=${join_changed_pixels} changed_alpha=${join_source_alpha_changed_pixels} official=0 parity=false")
# FOURTEENTH original source Q8 join-neighbor A/B: the same isolated
# miter formula as #305, sampled over additional adjacent device cells.
# Freeze independently validated miter parent; no improvement tolerance.
if(NOT join_abs_rgb_error EQUAL 509
   OR NOT join_changed_pixels EQUAL 21
   OR NOT join_gross_delta64_pixels EQUAL 0
   OR NOT join_max_channel_delta EQUAL 18
   OR NOT join_rgb_fnv64 STREQUAL "439859a3b614efea")
  message(FATAL_ERROR "Previously measured #305 Q8 miter control changed")
endif()
foreach(field IN ITEMS status default_changed pixel_parity_qualified
    repeat_equal source_alpha_changed_pixels
    outside_source_neighborhood alpha_delta_gt64_pixels
    rgb_changed_outside_source_alpha
    rgba_abs_rgb_error rgba_changed_pixels rgba_fnv64
    abs_rgb_error mean_abs_rgb_error changed_pixels
    gross_delta64_pixels max_channel_delta rgb_fnv64)
  string(JSON neighbor_${field} GET "${json}" adjacent_join_${field})
endforeach()
string(LENGTH "${neighbor_rgb_fnv64}" neighbor_hashlen)
string(LENGTH "${neighbor_rgba_fnv64}" neighbor_rgba_hashlen)
if(NOT neighbor_status STREQUAL "measured_unqualified"
   OR neighbor_default_changed OR neighbor_pixel_parity_qualified
   OR NOT neighbor_repeat_equal
   OR neighbor_source_alpha_changed_pixels LESS 1
   OR neighbor_source_alpha_changed_pixels GREATER 100
   OR NOT neighbor_outside_source_neighborhood EQUAL 0
   OR NOT neighbor_alpha_delta_gt64_pixels EQUAL 0
   OR NOT neighbor_rgb_changed_outside_source_alpha EQUAL 0
   OR neighbor_rgba_abs_rgb_error LESS 0
   OR neighbor_rgba_abs_rgb_error GREATER 383901480
   OR neighbor_rgba_changed_pixels LESS 0
   OR neighbor_rgba_changed_pixels GREATER 501832
   OR NOT neighbor_rgba_hashlen EQUAL 16
   OR NOT neighbor_rgba_fnv64 MATCHES "^[0-9a-f]+$"
   OR neighbor_abs_rgb_error LESS 0
   OR neighbor_abs_rgb_error GREATER 383901480
   OR neighbor_mean_abs_rgb_error LESS 0
   OR neighbor_mean_abs_rgb_error GREATER 255
   OR neighbor_changed_pixels LESS 0
   OR neighbor_changed_pixels GREATER 501832
   OR neighbor_gross_delta64_pixels LESS 0
   OR neighbor_gross_delta64_pixels GREATER neighbor_changed_pixels
   OR neighbor_max_channel_delta LESS 0
   OR neighbor_max_channel_delta GREATER 255
   OR NOT neighbor_hashlen EQUAL 16
   OR NOT neighbor_rgb_fnv64 MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Q8 adjacent-join hypothesis escaped source scope")
endif()
message(STATUS
  "jspdf-q8-adjacent-join: parent_rgb=${join_abs_rgb_error} trial_rgb=${neighbor_abs_rgb_error} parent_changed=${join_changed_pixels} trial_changed=${neighbor_changed_pixels} alpha_changes=${neighbor_source_alpha_changed_pixels} official=0 parity=false")
# Final read-only mode10 source residual accounting. This work occurs
# entirely after the existing private Q8 raster and original PDFium
# oracle have produced their images. All 501,832 pixels are included.
string(JSON nine_schema GET "${json}" adjacent_join_residual_regions schema_version)
string(JSON nine_status GET "${json}" adjacent_join_residual_regions observation_only)
string(JSON nine_parity GET "${json}" adjacent_join_residual_regions pixel_parity_qualified)
string(JSON nine_count LENGTH "${json}" adjacent_join_residual_regions regions)
string(JSON nine_pixels GET "${json}" adjacent_join_residual_regions total_pixels)
string(JSON nine_changed GET "${json}" adjacent_join_residual_regions total_changed)
string(JSON nine_error GET "${json}" adjacent_join_residual_regions total_abs_rgb_error)
string(JSON nine_gross GET "${json}" adjacent_join_residual_regions total_gross)
string(JSON nine_max GET "${json}" adjacent_join_residual_regions max_channel_delta)
string(JSON nine_complete GET "${json}" adjacent_join_residual_regions full_original_reconciled)
string(JSON nine_worst LENGTH "${json}" adjacent_join_residual_regions worst_pixels)
if(NOT nine_schema EQUAL 1 OR NOT nine_status OR nine_parity
   OR NOT nine_count EQUAL 16 OR NOT nine_complete
   OR NOT nine_pixels EQUAL 501832
   OR NOT nine_changed EQUAL neighbor_changed_pixels
   OR NOT nine_error EQUAL neighbor_abs_rgb_error
   OR NOT nine_gross EQUAL neighbor_gross_delta64_pixels
   OR NOT nine_max EQUAL neighbor_max_channel_delta)
  message(FATAL_ERROR "Mode10 actual source 16-bin audit does not reconcile")
endif()
if(nine_changed GREATER 16)
  if(NOT nine_worst EQUAL 16)
    message(FATAL_ERROR "Mode10 residual witness count was truncated wrongly")
  endif()
elseif(NOT nine_worst EQUAL nine_changed)
  message(FATAL_ERROR "Mode10 residual witness count mismatch")
endif()
set(nine_bin_pixels 0)
set(nine_bin_changed 0)
set(nine_bin_error 0)
set(nine_bin_gross 0)
foreach(name IN ITEMS bottom_left_endpoint bottom_right_endpoint
    bottom_single_horizontal_interior top_repeated_left_endpoint
    top_repeated_right_endpoint top_repeated_horizontal_interior
    right_vertical_stroke left_vertical_below_red
    red_left_w_clip_stroke_overlap red_left_w_clip_only
    red_right_fill_boundary red_top_right_coupled_corner
    red_top_stroke_overlap red_fill_bottom_edge
    red_bottom_uncovered_fringe other)
  string(JSON value_pixels GET "${json}" adjacent_join_residual_regions regions "${name}" pixels)
  string(JSON value_changed GET "${json}" adjacent_join_residual_regions regions "${name}" changed)
  string(JSON value_error GET "${json}" adjacent_join_residual_regions regions "${name}" abs_rgb_error)
  string(JSON value_gross GET "${json}" adjacent_join_residual_regions regions "${name}" gross)
  string(JSON value_bright GET "${json}" adjacent_join_residual_regions regions "${name}" brighter_rgb_error)
  string(JSON value_dark GET "${json}" adjacent_join_residual_regions regions "${name}" darker_rgb_error)
  math(EXPR signed_sum "${value_bright}+${value_dark}")
  if(NOT signed_sum EQUAL value_error OR value_changed GREATER value_pixels
     OR value_gross GREATER value_changed)
    message(FATAL_ERROR "Mode10 source bucket has inconsistent signed RGB accounting")
  endif()
  math(EXPR nine_bin_pixels "${nine_bin_pixels}+${value_pixels}")
  math(EXPR nine_bin_changed "${nine_bin_changed}+${value_changed}")
  math(EXPR nine_bin_error "${nine_bin_error}+${value_error}")
  math(EXPR nine_bin_gross "${nine_bin_gross}+${value_gross}")
endforeach()
if(NOT nine_bin_pixels EQUAL nine_pixels OR
   NOT nine_bin_changed EQUAL nine_changed OR
   NOT nine_bin_error EQUAL nine_error OR
   NOT nine_bin_gross EQUAL nine_gross)
  message(FATAL_ERROR "Mode10 source bins do not add up to original page")
endif()
message(STATUS
  "jspdf-post-mode10-16-bins: source_pixels=${nine_pixels} mismatches=${nine_changed} rgb=${nine_error} max=${nine_max} official=0 parity=false")
# FIFTEENTH independent original-source A/B: Q8 upper W-top / Fill-right
# coupling of ONE source clipped Fill device cell, not a renderer fallback.
# Preserve frozen #306 source geometry, original PDFium and report.
if(NOT neighbor_abs_rgb_error EQUAL 161
   OR NOT neighbor_changed_pixels EQUAL 9
   OR NOT neighbor_gross_delta64_pixels EQUAL 0
   OR NOT neighbor_max_channel_delta EQUAL 18
   OR NOT neighbor_rgb_fnv64 STREQUAL "91f25b6668d58b7c")
  message(FATAL_ERROR "Previously verified #306 source RGB digest changed")
endif()
foreach(field IN ITEMS status default_changed pixel_parity_qualified
    repeat_equal source_alpha_changed_pixels outside_corner
    alpha_delta_gt64_pixels rgb_changed_outside_source_alpha
    rgba_abs_rgb_error rgba_changed_pixels rgba_fnv64
    abs_rgb_error mean_abs_rgb_error changed_pixels
    gross_delta64_pixels max_channel_delta rgb_fnv64)
  string(JSON corner_${field} GET "${json}" top_right_fill_${field})
endforeach()
string(LENGTH "${corner_rgb_fnv64}" corner_hashlen)
string(LENGTH "${corner_rgba_fnv64}" corner_rgba_hashlen)
if(NOT corner_status STREQUAL "measured_unqualified"
   OR corner_default_changed OR corner_pixel_parity_qualified
   OR NOT corner_repeat_equal
   OR NOT corner_source_alpha_changed_pixels EQUAL 1
   OR NOT corner_outside_corner EQUAL 0
   OR NOT corner_alpha_delta_gt64_pixels EQUAL 0
   OR NOT corner_rgb_changed_outside_source_alpha EQUAL 0
   OR corner_rgba_abs_rgb_error LESS 0
   OR corner_rgba_abs_rgb_error GREATER 383901480
   OR corner_rgba_changed_pixels LESS 0
   OR corner_rgba_changed_pixels GREATER 501832
   OR NOT corner_rgba_hashlen EQUAL 16
   OR NOT corner_rgba_fnv64 MATCHES "^[0-9a-f]+$"
   OR corner_abs_rgb_error LESS 0
   OR corner_abs_rgb_error GREATER 383901480
   OR corner_mean_abs_rgb_error LESS 0
   OR corner_mean_abs_rgb_error GREATER 255
   OR corner_changed_pixels LESS 0
   OR corner_changed_pixels GREATER 501832
   OR corner_gross_delta64_pixels LESS 0
   OR corner_gross_delta64_pixels GREATER corner_changed_pixels
   OR corner_max_channel_delta LESS 0
   OR corner_max_channel_delta GREATER 255
   OR NOT corner_hashlen EQUAL 16
   OR NOT corner_rgb_fnv64 MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Private source Fill/W coupled corner left its isolation window")
endif()
message(STATUS
  "jspdf-q8-upper-right-fill-corner: parent_rgb=${neighbor_abs_rgb_error} trial_rgb=${corner_abs_rgb_error} parent_changed=${neighbor_changed_pixels} trial_changed=${corner_changed_pixels} source_alpha_changes=${corner_source_alpha_changed_pixels} parity=false official=0")
file(SHA256 "${SOURCE}" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "Original jsPDF source mutated by A/B")
endif()
message(STATUS
  "jspdf-single-vertical-analytic: status=measured_unqualified old_rgb=${got_mode3_abs_rgb_error} new_rgb=${got_mode4_abs_rgb_error} changed=${got_mode4_changed_pixels} gross=${got_mode4_gross_delta64_pixels} source_alpha_changes=${got_source_stroke_alpha_changed_pixels} official=0")
