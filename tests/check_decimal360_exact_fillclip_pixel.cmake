cmake_minimum_required(VERSION 3.20)
# #360: source PDF -> private Graph exact Grid -> Snapshot drop ->
# detached Owned IR -> unchanged native Fill/W adapter with explicit Grid.
# PDFium is ONLY the original independent oracle; never a fallback.
if(NOT DEFINED ORACLE_EXECUTABLE OR ORACLE_EXECUTABLE STREQUAL "" OR
   NOT DEFINED REPORT_FILE OR REPORT_FILE STREQUAL "")
  message(FATAL_ERROR "Explicit exact Fill/W source oracle and JSON report required")
endif()
file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(
  COMMAND "${ORACLE_EXECUTABLE}"
    --decimal360-exact-fillclip-pixel-ab
    --pixel-report "${REPORT_FILE}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 80)
if(NOT rc STREQUAL "0")
  message(FATAL_ERROR "Original Fill/W exact-grid opt-in failed (rc=${rc})"
    "\nstdout=${stdout}\nstderr=${stderr}")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "Missing atomic exact-grid Fill/W pixel diagnostics")
endif()
file(READ "${REPORT_FILE}" report)
foreach(required IN ITEMS
    "\"source_kind\":\"test_generated_original_pdf\""
    "\"source_producer\":\"QPDF_original_not_composer_reexport\""
    "\"exact_grid_supplied_before_owner_drop\":true"
    "\"new_renderer_opt_in_test_only\":true"
    "\"legacy_renderer_unchanged\":true"
    "\"pdfium_original_test_oracle_only\":true"
    "\"native_positive_provenance_increment\":0"
    "\"public_sdk_backend_switched\":false")
  string(FIND "${report}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Missing strict provenance/nonpromotion marker: ${required}")
  endif()
endforeach()
string(JSON cases LENGTH "${report}" frames)
string(JSON source_count GET "${report}" original_source_count)
string(JSON bytes_count GET "${report}" source_bytes_unchanged_count)
string(JSON frame_count GET "${report}" frame_count)
string(JSON pdfium_repeat GET "${report}" pdfium_repeat_count)
string(JSON exact_matches GET "${report}" exact_grid_pdfium_size_matches)
string(JSON raw_mismatches GET "${report}" legacy_grid_dimension_discrepancies)
string(JSON old_same_status GET "${report}" legacy_size_same_status_cases)
string(JSON old_native_rgba GET "${report}" legacy_native_rgba_exact_matches)
string(JSON native_accepted GET "${report}" new_native_admitted_frames)
string(JSON native_rejected GET "${report}" new_native_rejected_frames)
string(JSON repeat_native GET "${report}" new_native_repeat_identical_frames)
string(JSON negative_count GET "${report}" negative_zero_output_cases)
string(JSON error_sum GET "${report}" total_native_rgb_abs_error)
string(JSON different_sum GET "${report}" total_native_different_pixels)
string(JSON gross_sum GET "${report}" total_native_gross_pixels)
if(NOT cases EQUAL 48 OR NOT source_count EQUAL 16 OR
   NOT bytes_count EQUAL 16 OR NOT frame_count EQUAL 48 OR
   NOT pdfium_repeat EQUAL 48 OR NOT exact_matches EQUAL 48 OR
   NOT negative_count EQUAL 240 OR NOT repeat_native EQUAL native_accepted OR
   NOT native_accepted GREATER 0 OR NOT frame_count EQUAL 48)
  message(FATAL_ERROR "Incomplete original-PDF exact-grid Fill/W conformance ledger")
endif()
set(expected_dpis 72 96 144)
set(mismatches 0)
set(accepted_count 0)
set(pixel_errors 0)
set(native_differ 0)
set(gross_errors 0)
set(equal_status 0)
foreach(i RANGE 0 47)
  string(JSON crop GET "${report}" frames ${i} crop)
  string(JSON rotation GET "${report}" frames ${i} rotation)
  string(JSON dpi GET "${report}" frames ${i} dpi)
  string(JSON index GET "${report}" frames ${i} frame)
  string(JSON w GET "${report}" frames ${i} exact_grid 0)
  string(JSON h GET "${report}" frames ${i} exact_grid 1)
  string(JSON pw GET "${report}" frames ${i} pdfium_grid 0)
  string(JSON ph GET "${report}" frames ${i} pdfium_grid 1)
  string(JSON lw GET "${report}" frames ${i} legacy_grid 0)
  string(JSON lh GET "${report}" frames ${i} legacy_grid 1)
  string(JSON old_matches GET "${report}" frames ${i} legacy_size_matches)
  string(JSON native_status GET "${report}" frames ${i} exact_native_status)
  string(JSON legacy_status GET "${report}" frames ${i} legacy_native_status)
  string(JSON rgb_error GET "${report}" frames ${i} native_rgb_abs_error)
  string(JSON diff_pixels GET "${report}" frames ${i} native_different_pixels)
  string(JSON gross_pixels GET "${report}" frames ${i} native_gross_pixels)
  math(EXPR expected_crop "${i}/12")
  math(EXPR expected_rotate "(${i}%12)/3*90")
  math(EXPR dpi_index "${i}%3")
  list(GET expected_dpis ${dpi_index} expected_dpi)
  if(NOT index EQUAL i OR NOT crop EQUAL expected_crop OR
     NOT rotation EQUAL expected_rotate OR NOT dpi EQUAL expected_dpi OR
     w LESS 1 OR h LESS 1 OR NOT w EQUAL pw OR NOT h EQUAL ph)
    message(FATAL_ERROR "Invalid exact original PDFium sizing or fixture order")
  endif()
  if(old_matches)
    if(NOT w EQUAL lw OR NOT h EQUAL lh OR
       NOT native_status EQUAL legacy_status)
      message(FATAL_ERROR "Existing Fill/W preview behavior unexpectedly changed")
    endif()
    math(EXPR equal_status "${equal_status}+1")
  else()
    if(w EQUAL lw AND h EQUAL lh)
      message(FATAL_ERROR "False legacy-grid discrepancy report")
    endif()
    math(EXPR mismatches "${mismatches}+1")
  endif()
  if(native_status EQUAL 0)
    math(EXPR accepted_count "${accepted_count}+1")
  endif()
  math(EXPR pixel_errors "${pixel_errors}+${rgb_error}")
  math(EXPR native_differ "${native_differ}+${diff_pixels}")
  math(EXPR gross_errors "${gross_errors}+${gross_pixels}")
endforeach()
math(EXPR total_native "${native_accepted}+${native_rejected}")
if(NOT total_native EQUAL 48 OR
   NOT mismatches EQUAL raw_mismatches OR
   NOT equal_status EQUAL old_same_status OR
   NOT accepted_count EQUAL native_accepted OR
   NOT pixel_errors EQUAL error_sum OR
   NOT native_differ EQUAL different_sum OR
   NOT gross_errors EQUAL gross_sum)
  message(FATAL_ERROR "Frame and aggregate raster/grid observations disagree")
endif()
message(STATUS
  "#360 source QPDF + PDFium + exact C11 Fill/W diagnostic: "
  "16 immutable original PDFs, 48 repeated frames, "
  "${native_accepted}/48 opted-in native admits, "
  "${raw_mismatches}/48 legacy wrong sizes, "
  "${different_sum} RGB-difference pixels, ${error_sum} absolute RGB error, "
  "240/240 hard-zero negative opt-in refusals. "
  "Observed metrics ONLY — no external pixel-positive promotion or SDK switch.")
