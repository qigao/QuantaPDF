cmake_minimum_required(VERSION 3.20)

# No product normalization or renderer substitution. Original QPDF-generated
# bytes are independently rendered by PDFium; two private numeric grids are
# compared strictly as observations, including existing v0 fail-closed limits.
if(NOT DEFINED ORACLE_EXECUTABLE OR ORACLE_EXECUTABLE STREQUAL "" OR
   NOT DEFINED REPORT_FILE OR REPORT_FILE STREQUAL "")
  message(FATAL_ERROR "Explicit decimal CropBox oracle and report required")
endif()
file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(
  COMMAND "${ORACLE_EXECUTABLE}"
    --generated-unit7-decimal-crop-ab
    --decimal-crop-report "${REPORT_FILE}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 35)
if(NOT rc STREQUAL "0")
  message(FATAL_ERROR
    "Source-backed decimal CropBox A/B oracle failed:\n"
    "stdout=${out}\nstderr=${err}\nrc=${rc}")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "Atomic source-backed decimal CropBox report absent")
endif()
file(READ "${REPORT_FILE}" report)
foreach(marker IN ITEMS
  "\"source_kind\":\"test_generated_original_pdf\""
  "\"source_writer\":\"test_owned_qpdf_writer\""
  "\"decimal_candidate_test_only\":true"
  "\"original_source_count\":16"
  "\"source_bytes_unchanged_count\":16"
  "\"pdfium_repeat_count\":48"
  "\"frame_count\":48"
  "\"private_unsupported_dpi_zero_refusals\":32"
  "\"private_v0_allowlist_unchanged\":true"
  "\"decimal_rational_candidate_mismatch_cases\":0"
  "\"candidate_writes_source_or_graph\":false"
  "\"original_pdfium_pixels_are_observations_only\":true"
  "\"external_native_positive_increment\":0"
  "\"public_sdk_backend_switched\":false")
  string(FIND "${report}" "${marker}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Decimal CropBox A/B/nonpromotion field missing: ${marker}")
  endif()
endforeach()
string(JSON frames_count LENGTH "${report}" frames)
string(JSON dpi_order_count LENGTH "${report}" dpi_order)
string(JSON crop_count LENGTH "${report}" raw_mismatch_by_crop)
string(JSON dpi_count LENGTH "${report}" raw_mismatch_by_dpi)
if(NOT frames_count EQUAL 48 OR NOT dpi_order_count EQUAL 3 OR
   NOT crop_count EQUAL 4 OR NOT dpi_count EQUAL 3)
  message(FATAL_ERROR "Exactly 16 original PDFs × 3 supported DPIs required")
endif()
set(dpis 72 96 144)
set(unrotated_width 70 94 140)
set(unrotated_height 56 75 112)
foreach(d RANGE 0 2)
  list(GET dpis ${d} expected)
  string(JSON observed GET "${report}" dpi_order ${d})
  if(NOT observed EQUAL expected)
    message(FATAL_ERROR "Original-DPI allowlist order changed")
  endif()
  set(mismatch_dpi_${d} 0)
endforeach()
foreach(c RANGE 0 3)
  set(mismatch_crop_${c} 0)
endforeach()
set(total_mismatch 0)
set(candidate_mismatch 0)
set(extra_width 0)
set(extra_height 0)
set(raw_points_w_positive 0)
set(raw_points_h_positive 0)
set(sum_pixels 0)
foreach(i RANGE 0 47)
  string(JSON crop GET "${report}" frames ${i} crop_variant)
  string(JSON rotation GET "${report}" frames ${i} rotation)
  string(JSON dpi GET "${report}" frames ${i} dpi)
  string(JSON pdfium_count LENGTH "${report}" frames ${i} pdfium_size)
  string(JSON graph_count LENGTH "${report}" frames ${i} raw_graph_grid)
  string(JSON rational_count LENGTH "${report}" frames ${i} rational_test_grid)
  string(JSON raw_pts_count LENGTH "${report}" frames ${i} raw_display_points)
  string(JSON rat_pts_count LENGTH "${report}" frames ${i} rational_display_points)
  string(JSON excess GET "${report}" frames ${i} raw_grid_excess_pixels)
  string(JSON corrected_excess GET "${report}" frames ${i} rational_grid_excess_pixels)
  if(NOT pdfium_count EQUAL 2 OR NOT graph_count EQUAL 2 OR
     NOT rational_count EQUAL 2 OR
     NOT raw_pts_count EQUAL 2 OR NOT rat_pts_count EQUAL 2)
    message(FATAL_ERROR "Incomplete original/Graph/rational width/height observation")
  endif()
  math(EXPR expected_crop "${i}/12")
  math(EXPR expected_rotation "(${i}%12)/3 * 90")
  math(EXPR dindex "${i}%3")
  list(GET dpis ${dindex} expected_dpi)
  list(GET unrotated_width ${dindex} native_w)
  list(GET unrotated_height ${dindex} native_h)
  if(rotation EQUAL 90 OR rotation EQUAL 270)
    set(expected_w "${native_h}")
    set(expected_h "${native_w}")
  else()
    set(expected_w "${native_w}")
    set(expected_h "${native_h}")
  endif()
  if(NOT crop EQUAL expected_crop OR
     NOT rotation EQUAL expected_rotation OR NOT dpi EQUAL expected_dpi)
    message(FATAL_ERROR "Original page/rotation/DPI source order unexpectedly changed")
  endif()
  string(JSON pdf_w GET "${report}" frames ${i} pdfium_size 0)
  string(JSON pdf_h GET "${report}" frames ${i} pdfium_size 1)
  string(JSON graph_w GET "${report}" frames ${i} raw_graph_grid 0)
  string(JSON graph_h GET "${report}" frames ${i} raw_graph_grid 1)
  string(JSON rat_w GET "${report}" frames ${i} rational_test_grid 0)
  string(JSON rat_h GET "${report}" frames ${i} rational_test_grid 1)
  string(JSON raw_pt_w GET "${report}" frames ${i} raw_display_points 0)
  string(JSON raw_pt_h GET "${report}" frames ${i} raw_display_points 1)
  string(JSON rat_pt_w GET "${report}" frames ${i} rational_display_points 0)
  string(JSON rat_pt_h GET "${report}" frames ${i} rational_display_points 1)
  math(EXPR graph_diff_w "${graph_w}-${pdf_w}")
  math(EXPR graph_diff_h "${graph_h}-${pdf_h}")
  math(EXPR combined_difference "${graph_diff_w}+${graph_diff_h}")
  if(NOT pdf_w EQUAL expected_w OR NOT pdf_h EQUAL expected_h OR
     NOT rat_w EQUAL pdf_w OR NOT rat_h EQUAL pdf_h OR
     NOT corrected_excess EQUAL 0 OR
     graph_diff_w LESS 0 OR graph_diff_w GREATER 1 OR
     graph_diff_h LESS 0 OR graph_diff_h GREATER 1 OR
     NOT excess EQUAL combined_difference)
    message(FATAL_ERROR "Independent source PDFium vs Graph/rational grid mismatch invalid")
  endif()
  if(combined_difference GREATER 0)
    math(EXPR total_mismatch "${total_mismatch}+1")
    math(EXPR mismatch_crop_${crop} "${mismatch_crop_${crop}}+1")
    math(EXPR mismatch_dpi_${dindex} "${mismatch_dpi_${dindex}}+1")
  endif()
  if(graph_diff_w GREATER 0)
    math(EXPR extra_width "${extra_width}+1")
  endif()
  if(graph_diff_h GREATER 0)
    math(EXPR extra_height "${extra_height}+1")
  endif()
  if(NOT rat_pt_w GREATER 0 OR NOT rat_pt_h GREATER 0 OR
     NOT raw_pt_w GREATER 0 OR NOT raw_pt_h GREATER 0)
    message(FATAL_ERROR "Page display width/height must be positive finite doubles")
  endif()
  if(dindex EQUAL 0)
    if(raw_pt_w GREATER rat_pt_w)
      math(EXPR raw_points_w_positive "${raw_points_w_positive}+1")
    endif()
    if(raw_pt_h GREATER rat_pt_h)
      math(EXPR raw_points_h_positive "${raw_points_h_positive}+1")
    endif()
  endif()
  math(EXPR sum_pixels "${sum_pixels}+${pdf_w}*${pdf_h}")
endforeach()
if(NOT sum_pixels EQUAL 426400 OR total_mismatch LESS 1)
  message(FATAL_ERROR "No reproducible original binary decimal grid discrepancy")
endif()
foreach(c RANGE 0 3)
  string(JSON reported GET "${report}" raw_mismatch_by_crop ${c})
  if(NOT reported EQUAL mismatch_crop_${c})
    message(FATAL_ERROR "Cropping-specific decimal mismatch report disagrees with original frames")
  endif()
endforeach()
foreach(d RANGE 0 2)
  string(JSON reported GET "${report}" raw_mismatch_by_dpi ${d})
  if(NOT reported EQUAL mismatch_dpi_${d})
    message(FATAL_ERROR "DPI-specific decimal mismatch report disagrees with original frames")
  endif()
endforeach()
foreach(field IN ITEMS
  graph_to_device_raw_mismatch_cases
  decimal_rational_candidate_mismatch_cases
  raw_extra_column_cases
  raw_extra_row_cases
  raw_page_width_above_rational_cases
  raw_page_height_above_rational_cases)
  string(JSON result GET "${report}" ${field})
  if(field STREQUAL "graph_to_device_raw_mismatch_cases")
    set(expected "${total_mismatch}")
  elseif(field STREQUAL "decimal_rational_candidate_mismatch_cases")
    set(expected 0)
  elseif(field STREQUAL "raw_extra_column_cases")
    set(expected "${extra_width}")
  elseif(field STREQUAL "raw_extra_row_cases")
    set(expected "${extra_height}")
  elseif(field STREQUAL "raw_page_width_above_rational_cases")
    set(expected "${raw_points_w_positive}")
  else()
    set(expected "${raw_points_h_positive}")
  endif()
  if(NOT result EQUAL expected)
    message(FATAL_ERROR "Decimal candidate aggregate inconsistent with 48 real PDFium modes: ${field}")
  endif()
endforeach()
message(STATUS
  "16 actual test-generated original PDFs × 72/96/144 DPI: "
  "48 repeated PDFium images, 32 unsupported DPI 120/168 refusals, "
  "${total_mismatch} Graph raw-grid mismatches, 0 integer-thousandth "
  "test-only candidate mismatches, extra-column=${extra_width} "
  "extra-row=${extra_height}; NO parser change/SDK switching.")
