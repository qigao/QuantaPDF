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
file(SHA256 "${SOURCE}" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "Original jsPDF source mutated by A/B")
endif()
message(STATUS
  "jspdf-single-vertical-analytic: status=measured_unqualified old_rgb=${got_mode3_abs_rgb_error} new_rgb=${got_mode4_abs_rgb_error} changed=${got_mode4_changed_pixels} gross=${got_mode4_gross_delta64_pixels} source_alpha_changes=${got_source_stroke_alpha_changed_pixels} official=0")
