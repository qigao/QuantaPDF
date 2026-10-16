cmake_minimum_required(VERSION 3.20)
# #269 HELD-OUT original-PDF source test: offline Q4 alpha thresholds and
# independent exact-grid pixel-center geometry. PDFium is an oracle, not fallback.
# No candidate is promoted to the native renderer by this conformance test.
if(NOT DEFINED ORACLE_EXECUTABLE OR ORACLE_EXECUTABLE STREQUAL "" OR
   NOT DEFINED REPORT_FILE OR REPORT_FILE STREQUAL "")
  message(FATAL_ERROR "Explicit #269 holdout original-PDF oracle/report required")
endif()
file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(COMMAND "${ORACLE_EXECUTABLE}"
  --269-heldout-q4-threshold-center --holdout-report "${REPORT_FILE}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr
  TIMEOUT 95)
if(NOT rc STREQUAL "0")
  message(FATAL_ERROR "Held-out original PDF oracle failed rc=${rc}\n${stdout}\n${stderr}")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "Held-out original PDF report not committed atomically")
endif()
file(READ "${REPORT_FILE}" report)
foreach(marker IN ITEMS
   "\"source_kind\":\"test_generated_original_pdf\""
   "\"source_provenance\":\"new_heldout_qpdf_original_not_ir_export\""
   "\"source_shapes_holdout_from_269_370\":true"
   "\"pixel_center_uses_value_grid_without_pdfium_pixels\":true"
   "\"binary_center_uses_half_open_source_rectangles\":true"
   "\"thresholds_are_offline_observation_only\":true"
   "\"native_renderer_mutated\":false"
   "\"pdfium_fallback_used\":false"
   "\"third_party_native_positive_increment\":0"
   "\"public_sdk_backend_switched\":false")
  string(FIND "${report}" "${marker}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "Missing heldout/nonpromotion provenance: ${marker}")
  endif()
endforeach()
string(JSON source_n GET "${report}" original_source_count)
string(JSON verified_n GET "${report}" source_bytes_verified)
string(JSON repeat_n GET "${report}" original_pdfium_repeats)
string(JSON native_repeat GET "${report}" native_repeat_identical)
string(JSON frame_n GET "${report}" frames_count)
string(JSON all_pixels GET "${report}" pixels_examined)
string(JSON old_wrong GET "${report}" old_size_mismatches)
string(JSON old_equal GET "${report}" old_size_correct_rgba_identical)
string(JSON frames_len LENGTH "${report}" frames)
string(JSON dpi_len LENGTH "${report}" dpis)
string(JSON alpha_len LENGTH "${report}" native_alpha_q4_counts)
string(JSON method_len LENGTH "${report}" total_scores)
string(JSON scores_by_dpi_len LENGTH "${report}" scores_by_dpi)
string(JSON scores_by_shape_len LENGTH "${report}" scores_by_shape)
string(JSON alpha_off GET "${report}" native_alpha_off_q4_lattice)
if(NOT source_n EQUAL 32 OR NOT verified_n EQUAL 32 OR
   NOT repeat_n EQUAL 96 OR NOT native_repeat EQUAL 96 OR
   NOT frame_n EQUAL 96 OR NOT frames_len EQUAL 96 OR
   NOT old_wrong EQUAL 16 OR NOT old_equal EQUAL 80 OR
   NOT dpi_len EQUAL 3 OR NOT alpha_len EQUAL 17 OR
   NOT method_len EQUAL 7 OR
   NOT scores_by_dpi_len EQUAL 3 OR NOT scores_by_shape_len EQUAL 2 OR
   all_pixels LESS 500000)
  message(FATAL_ERROR "Held-out source/PDFium/native scope or count incomplete")
endif()
set(expected_dpis 72 96 144)
set(names native_q4_white_composite
  q4_alpha_threshold_1_of_16 q4_alpha_threshold_4_of_16
  q4_alpha_threshold_8_of_16 q4_alpha_threshold_12_of_16
  q4_alpha_threshold_16_of_16 independent_pixel_center)
foreach(method RANGE 0 6)
  set(abs_${method} 0)
  set(diff_${method} 0)
  set(gross_${method} 0)
  set(win_${method} 0)
  set(lose_${method} 0)
  set(draw_${method} 0)
  foreach(dpi_index RANGE 0 2)
    set(abs_dpi_${dpi_index}_${method} 0)
    set(diff_dpi_${dpi_index}_${method} 0)
    set(gross_dpi_${dpi_index}_${method} 0)
  endforeach()
  foreach(shape RANGE 0 1)
    set(abs_shape_${shape}_${method} 0)
    set(diff_shape_${shape}_${method} 0)
    set(gross_shape_${shape}_${method} 0)
  endforeach()
endforeach()
set(pixels_counted 0)
set(reference_partial_dpi_0 0)
set(reference_partial_dpi_1 0)
set(reference_partial_dpi_2 0)
set(center_ties_dpi_0 0)
set(center_ties_dpi_1 0)
set(center_ties_dpi_2 0)
set(old_wrong_counted 0)
set(last_source "")
foreach(i RANGE 0 95)
  string(JSON frame GET "${report}" frames ${i} frame)
  string(JSON shape GET "${report}" frames ${i} shape)
  string(JSON crop GET "${report}" frames ${i} crop)
  string(JSON rotation GET "${report}" frames ${i} rotation)
  string(JSON dpi GET "${report}" frames ${i} dpi)
  string(JSON fnv GET "${report}" frames ${i} source_fnv64)
  string(JSON w GET "${report}" frames ${i} exact_grid 0)
  string(JSON h GET "${report}" frames ${i} exact_grid 1)
  string(JSON old_w GET "${report}" frames ${i} legacy_grid 0)
  string(JSON old_h GET "${report}" frames ${i} legacy_grid 1)
  string(JSON old_match GET "${report}" frames ${i} legacy_size_same)
  string(JSON reference_partial GET "${report}" frames ${i} reference_partial_pixels)
  string(JSON center_ties GET "${report}" frames ${i} center_boundary_ties)
  string(JSON methods LENGTH "${report}" frames ${i} scores)
  math(EXPR want_shape "${i}/48")
  math(EXPR want_crop "(${i}%48)/12")
  math(EXPR want_rotation "((${i}%12)/3)*90")
  math(EXPR dpi_index "${i}%3")
  list(GET expected_dpis ${dpi_index} want_dpi)
  string(LENGTH "${fnv}" fnv_length)
  math(EXPR pixels "${w}*${h}")
  math(EXPR rgb_max "${pixels}*765")
  if(NOT frame EQUAL i OR NOT shape EQUAL want_shape OR
     NOT crop EQUAL want_crop OR NOT rotation EQUAL want_rotation OR
     NOT dpi EQUAL want_dpi OR NOT methods EQUAL 7 OR
     NOT fnv_length EQUAL 16 OR NOT fnv MATCHES "^[0-9a-f]+$" OR
     pixels LESS 1 OR pixels GREATER 22000 OR
     reference_partial GREATER pixels OR center_ties GREATER pixels)
    message(FATAL_ERROR "#269 heldout frame/identity/order/dimension invalid")
  endif()
  if(dpi_index EQUAL 0)
    set(last_source "${fnv}")
  elseif(NOT fnv STREQUAL last_source)
    message(FATAL_ERROR "Same immutable source PDF mutated across three DPI renders")
  endif()
  if(old_match)
    if(NOT old_w EQUAL w OR NOT old_h EQUAL h)
      message(FATAL_ERROR "Old Grid marked matching but sizes differ")
    endif()
  else()
    if(old_w EQUAL w AND old_h EQUAL h)
      message(FATAL_ERROR "False old-grid mismatch reported")
    endif()
    math(EXPR old_wrong_counted "${old_wrong_counted}+1")
  endif()
  math(EXPR pixels_counted "${pixels_counted}+${pixels}")
  math(EXPR reference_partial_dpi_${dpi_index}
    "${reference_partial_dpi_${dpi_index}}+${reference_partial}")
  math(EXPR center_ties_dpi_${dpi_index}
    "${center_ties_dpi_${dpi_index}}+${center_ties}")
  foreach(method RANGE 0 6)
    list(GET names ${method} expected_name)
    string(JSON name GET "${report}" frames ${i} scores ${method} method)
    string(JSON rgb_abs GET "${report}" frames ${i} scores ${method} rgb_abs)
    string(JSON diff GET "${report}" frames ${i} scores ${method} different)
    string(JSON gross GET "${report}" frames ${i} scores ${method} gross)
    if(NOT name STREQUAL expected_name OR gross GREATER diff OR
       diff GREATER pixels OR rgb_abs GREATER rgb_max)
      message(FATAL_ERROR "Held-out candidate score invalid")
    endif()
    math(EXPR abs_${method} "${abs_${method}}+${rgb_abs}")
    math(EXPR diff_${method} "${diff_${method}}+${diff}")
    math(EXPR gross_${method} "${gross_${method}}+${gross}")
    math(EXPR abs_dpi_${dpi_index}_${method}
      "${abs_dpi_${dpi_index}_${method}}+${rgb_abs}")
    math(EXPR diff_dpi_${dpi_index}_${method}
      "${diff_dpi_${dpi_index}_${method}}+${diff}")
    math(EXPR gross_dpi_${dpi_index}_${method}
      "${gross_dpi_${dpi_index}_${method}}+${gross}")
    math(EXPR abs_shape_${shape}_${method}
      "${abs_shape_${shape}_${method}}+${rgb_abs}")
    math(EXPR diff_shape_${shape}_${method}
      "${diff_shape_${shape}_${method}}+${diff}")
    math(EXPR gross_shape_${shape}_${method}
      "${gross_shape_${shape}_${method}}+${gross}")
    if(method EQUAL 0)
      set(baseline_frame_abs "${rgb_abs}")
    endif()
    if(rgb_abs LESS baseline_frame_abs)
      math(EXPR win_${method} "${win_${method}}+1")
    elseif(rgb_abs GREATER baseline_frame_abs)
      math(EXPR lose_${method} "${lose_${method}}+1")
    else()
      math(EXPR draw_${method} "${draw_${method}}+1")
    endif()
  endforeach()
endforeach()
if(NOT pixels_counted EQUAL all_pixels OR
   NOT old_wrong_counted EQUAL old_wrong)
  message(FATAL_ERROR "Held-out source pixel accounting mismatch")
endif()
foreach(dpi_index RANGE 0 2)
  string(JSON reported_partial GET "${report}"
    reference_partial_by_dpi ${dpi_index})
  string(JSON reported_ties GET "${report}"
    center_boundary_ties_by_dpi ${dpi_index})
  if(NOT reported_partial EQUAL reference_partial_dpi_${dpi_index} OR
     NOT reported_ties EQUAL center_ties_dpi_${dpi_index})
    message(FATAL_ERROR "DPI-specific heldout reference/tie counts changed")
  endif()
endforeach()
foreach(method RANGE 0 6)
  string(JSON total_abs GET "${report}" total_scores ${method} rgb_abs)
  string(JSON total_diff GET "${report}" total_scores ${method} different)
  string(JSON total_gross GET "${report}" total_scores ${method} gross)
  string(JSON win GET "${report}" frames_better_vs_q4 ${method})
  string(JSON draw GET "${report}" frames_equal_vs_q4 ${method})
  string(JSON lose GET "${report}" frames_worse_vs_q4 ${method})
  if(NOT total_abs EQUAL abs_${method} OR
     NOT total_diff EQUAL diff_${method} OR
     NOT total_gross EQUAL gross_${method} OR
     NOT win EQUAL win_${method} OR
     NOT draw EQUAL draw_${method} OR
     NOT lose EQUAL lose_${method})
    message(FATAL_ERROR "Held-out method total does not equal 96 frames")
  endif()
  math(EXPR scored_count "${win}+${draw}+${lose}")
  if(NOT scored_count EQUAL 96)
    message(FATAL_ERROR "Held-out method win/regression scope incomplete")
  endif()
  foreach(dpi_index RANGE 0 2)
    string(JSON a GET "${report}" scores_by_dpi ${dpi_index} ${method} rgb_abs)
    string(JSON d GET "${report}" scores_by_dpi ${dpi_index} ${method} different)
    string(JSON g GET "${report}" scores_by_dpi ${dpi_index} ${method} gross)
    if(NOT a EQUAL abs_dpi_${dpi_index}_${method} OR
       NOT d EQUAL diff_dpi_${dpi_index}_${method} OR
       NOT g EQUAL gross_dpi_${dpi_index}_${method})
      message(FATAL_ERROR "Held-out 72/96/144 DPI candidate ledger mismatch")
    endif()
  endforeach()
  foreach(shape RANGE 0 1)
    string(JSON a GET "${report}" scores_by_shape ${shape} ${method} rgb_abs)
    string(JSON d GET "${report}" scores_by_shape ${shape} ${method} different)
    string(JSON g GET "${report}" scores_by_shape ${shape} ${method} gross)
    if(NOT a EQUAL abs_shape_${shape}_${method} OR
       NOT d EQUAL diff_shape_${shape}_${method} OR
       NOT g EQUAL gross_shape_${shape}_${method})
      message(FATAL_ERROR "Held-out new shape candidate ledger mismatch")
    endif()
  endforeach()
endforeach()
set(alpha_total "${alpha_off}")
foreach(k RANGE 0 16)
  string(JSON n GET "${report}" native_alpha_q4_counts ${k})
  math(EXPR alpha_total "${alpha_total}+${n}")
endforeach()
if(NOT alpha_total EQUAL all_pixels)
  message(FATAL_ERROR "Actual native RGBA Q4 alpha histogram incomplete")
endif()
message(STATUS
  "#269 heldout Q4-vs-threshold-vs-center: 32 test-written original PDFs,"
  " 96 repeated independent PDFium frames / 96 exact native frames,"
  " ${all_pixels} pixels; 16 legacy incorrect sizes retained."
  " Base Q4 diff=${diff_0}, RGB abs=${abs_0}."
  " Center diff=${diff_6}, RGB abs=${abs_6}."
  " No threshold/center renderer mutation, backend switch or positive promotion.")
