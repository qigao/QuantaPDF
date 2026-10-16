cmake_minimum_required(VERSION 3.20)
# #269: strict independent original 96DPI Fill-only/W-only/combined mask ledger.
# PDFium never serves as a native fallback and these are test-owned sources.
if(NOT DEFINED ORACLE_EXECUTABLE OR ORACLE_EXECUTABLE STREQUAL "" OR
   NOT DEFINED REPORT_FILE OR REPORT_FILE STREQUAL "")
  message(FATAL_ERROR "Original-PDF 96dpi mask oracle and report required")
endif()
file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(COMMAND "${ORACLE_EXECUTABLE}"
  --unit7-96dpi-mask-ownership --mask-report "${REPORT_FILE}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 80)
if(NOT rc STREQUAL "0")
  message(FATAL_ERROR "#269 original-mask test failed: ${rc}\n${stdout}\n${stderr}")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "Missing transactional mask ownership report")
endif()
file(READ "${REPORT_FILE}" report)
foreach(marker IN ITEMS
  "\"source_kind\":\"test_generated_original_pdf\""
  "\"source_writer\":\"test_owned_qpdf_writer\""
  "\"origin\":\"three_unmodified_original_pdf_bytes_per_case\""
  "\"native_consumer\":\"explicit_exact_grid_fill_w_clip_test_only\""
  "\"primitive_reference\":\"original_pdfium_only\""
  "\"pdfium_fallback_used\":false"
  "\"native_renderer_modified\":false"
  "\"external_native_pixel_positive_increment\":0"
  "\"public_sdk_backend_switched\":false")
  string(FIND "${report}" "${marker}" hit)
  if(hit EQUAL -1)
    message(FATAL_ERROR "Missing original/provenance marker: ${marker}")
  endif()
endforeach()
string(JSON frames LENGTH "${report}" frames)
string(JSON sources GET "${report}" source_files)
string(JSON repeat_bytes GET "${report}" source_byte_repeat_count)
string(JSON reference_repeat GET "${report}" original_pdfium_repeat_frames)
string(JSON native_frames GET "${report}" native_exact_grid_frames)
string(JSON native_repeat GET "${report}" native_repeat_identical_frames)
string(JSON legacy_identical GET "${report}" legacy_native_rgba_identical_frames)
string(JSON total_pixels GET "${report}" pixels_examined)
string(JSON total_rgb GET "${report}" native_vs_original_rgb_abs)
string(JSON total_diff GET "${report}" native_vs_original_different)
string(JSON total_gross GET "${report}" native_vs_original_gross)
string(JSON total_prod_abs GET "${report}" original_combined_vs_product_abs)
string(JSON total_prod_diff GET "${report}" original_combined_vs_product_different)
if(NOT frames EQUAL 16 OR NOT sources EQUAL 48 OR
   NOT repeat_bytes EQUAL 48 OR NOT reference_repeat EQUAL 48 OR
   NOT native_frames EQUAL 16 OR NOT native_repeat EQUAL 16 OR
   NOT legacy_identical EQUAL 16 OR NOT total_pixels EQUAL 112800)
  message(FATAL_ERROR "Incomplete exact-grid original primitive-mask corpus")
endif()
# #367 independently established the combined 96 DPI full-image baseline.
# Retain the regression rather than pretending exact dimensions fix pixels.
if(NOT total_rgb EQUAL 567648 OR NOT total_diff EQUAL 2520)
  message(FATAL_ERROR "Old 96dpi Fill/W original RGB mismatch changed")
endif()
set(region_names outside_primitives opaque_both fill_edge_only
                 clip_edge_only both_partial)
set(sum_frame_rgb 0)
set(sum_frame_diff 0)
set(sum_frame_gross 0)
set(sum_product_abs 0)
set(sum_product_diff 0)
foreach(i RANGE 0 15)
  string(JSON crop GET "${report}" frames ${i} crop)
  string(JSON rotation GET "${report}" frames ${i} rotation)
  string(JSON dpi GET "${report}" frames ${i} dpi)
  string(JSON w GET "${report}" frames ${i} size 0)
  string(JSON h GET "${report}" frames ${i} size 1)
  string(JSON originals LENGTH "${report}" frames ${i} original_pdf_bytes_fnv64)
  string(JSON mask_count LENGTH "${report}" frames ${i} mask_nonzero)
  string(JSON regions LENGTH "${report}" frames ${i} regions)
  string(JSON ferr GET "${report}" frames ${i} native_vs_combined_rgb_abs)
  string(JSON fdiff GET "${report}" frames ${i} native_vs_combined_different)
  string(JSON fgross GET "${report}" frames ${i} native_vs_combined_gross)
  string(JSON pabs GET "${report}" frames ${i} original_combined_vs_product_abs)
  string(JSON pdiff GET "${report}" frames ${i} original_combined_vs_product_diff)
  math(EXPR crop_expected "${i}/4")
  math(EXPR rotation_expected "(${i}%4)*90")
  math(EXPR pixels "${w}*${h}")
  if(NOT crop EQUAL crop_expected OR NOT rotation EQUAL rotation_expected OR
     NOT dpi EQUAL 96 OR NOT pixels EQUAL 7050 OR NOT originals EQUAL 3 OR
     NOT mask_count EQUAL 3 OR NOT regions EQUAL 5)
    message(FATAL_ERROR "Incomplete/incorrect original mask frame geometry")
  endif()
  foreach(j RANGE 0 2)
    string(JSON fnv GET "${report}" frames ${i} original_pdf_bytes_fnv64 ${j})
    string(LENGTH "${fnv}" fnv_len)
    string(JSON nonzero GET "${report}" frames ${i} mask_nonzero ${j})
    string(JSON full GET "${report}" frames ${i} mask_full ${j})
    string(JSON partial GET "${report}" frames ${i} mask_partial ${j})
    if(NOT fnv_len EQUAL 16 OR NOT fnv MATCHES "^[0-9a-f]+$" OR
       nonzero LESS 1 OR full LESS 1 OR
       nonzero GREATER 7050 OR partial GREATER nonzero)
      message(FATAL_ERROR "Source original byte fingerprint or mask invalid")
    endif()
  endforeach()
  set(region_pixel_sum 0)
  set(region_error_sum 0)
  set(region_diff_sum 0)
  set(region_gross_sum 0)
  foreach(j RANGE 0 4)
    list(GET region_names ${j} region_expected)
    string(JSON name GET "${report}" frames ${i} regions ${j} name)
    string(JSON cnt GET "${report}" frames ${i} regions ${j} pixels)
    string(JSON err GET "${report}" frames ${i} regions ${j} rgb_abs)
    string(JSON diff GET "${report}" frames ${i} regions ${j} different)
    string(JSON gross GET "${report}" frames ${i} regions ${j} gross)
    if(NOT name STREQUAL region_expected OR diff GREATER cnt OR
       gross GREATER diff)
      message(FATAL_ERROR "Broken source-boundary ownership partition")
    endif()
    math(EXPR region_pixel_sum "${region_pixel_sum}+${cnt}")
    math(EXPR region_error_sum "${region_error_sum}+${err}")
    math(EXPR region_diff_sum "${region_diff_sum}+${diff}")
    math(EXPR region_gross_sum "${region_gross_sum}+${gross}")
  endforeach()
  if(NOT region_pixel_sum EQUAL 7050 OR
     NOT region_error_sum EQUAL ferr OR
     NOT region_diff_sum EQUAL fdiff OR
     NOT region_gross_sum EQUAL fgross)
    message(FATAL_ERROR "Frame-level region totals disagree with RGB oracle")
  endif()
  math(EXPR sum_frame_rgb "${sum_frame_rgb}+${ferr}")
  math(EXPR sum_frame_diff "${sum_frame_diff}+${fdiff}")
  math(EXPR sum_frame_gross "${sum_frame_gross}+${fgross}")
  math(EXPR sum_product_abs "${sum_product_abs}+${pabs}")
  math(EXPR sum_product_diff "${sum_product_diff}+${pdiff}")
endforeach()
string(JSON global_regions LENGTH "${report}" regions)
if(NOT global_regions EQUAL 5)
  message(FATAL_ERROR "Missing five boundary-ownership categories")
endif()
set(global_pixels 0)
set(global_rgb 0)
set(global_diff 0)
set(global_gross 0)
foreach(j RANGE 0 4)
  list(GET region_names ${j} expected)
  string(JSON name GET "${report}" regions ${j} name)
  string(JSON count GET "${report}" regions ${j} pixels)
  string(JSON err GET "${report}" regions ${j} rgb_abs)
  string(JSON diff GET "${report}" regions ${j} different)
  string(JSON gross GET "${report}" regions ${j} gross)
  if(NOT name STREQUAL expected OR diff GREATER count OR gross GREATER diff)
    message(FATAL_ERROR "Invalid global Fill/W region category")
  endif()
  math(EXPR global_pixels "${global_pixels}+${count}")
  math(EXPR global_rgb "${global_rgb}+${err}")
  math(EXPR global_diff "${global_diff}+${diff}")
  math(EXPR global_gross "${global_gross}+${gross}")
endforeach()
if(NOT global_pixels EQUAL 112800 OR NOT global_rgb EQUAL total_rgb OR
   NOT global_diff EQUAL total_diff OR NOT global_gross EQUAL total_gross OR
   NOT sum_frame_rgb EQUAL total_rgb OR
   NOT sum_frame_diff EQUAL total_diff OR
   NOT sum_frame_gross EQUAL total_gross OR
   NOT sum_product_abs EQUAL total_prod_abs OR
   NOT sum_product_diff EQUAL total_prod_diff)
  message(FATAL_ERROR "Original mask/region aggregate reconciliation failed")
endif()
message(STATUS "#269 96DPI original mask decomposition: 48 independent "
  "original PDFium frames, 16 correct-size native renders, "
  "112800 total pixels, 2520 native differing pixels / 567648 abs RGB, "
  "${total_prod_diff} original-combined vs Fill×Clip disagreements; "
  "no merge, fallback, external pixel-positive or SDK cutover.")
