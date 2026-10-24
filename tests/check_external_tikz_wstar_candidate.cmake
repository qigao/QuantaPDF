# Candidate-only pixel qualification for the unchanged TeXample PDF.
cmake_minimum_required(VERSION 3.24)
foreach(arg IN ITEMS ORACLE SOURCE REPORT EXPECTED_SHA256)
  if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${arg}")
  endif()
endforeach()

set(PINNED_SHA256 "f691ff46e64353ad285a5ddf24c82b956de67b5a6b3ed7d244167a301f36a284")
if(NOT EXPECTED_SHA256 STREQUAL PINNED_SHA256)
  message(FATAL_ERROR "Unexpected pinned TeXample source SHA-256")
endif()
file(SIZE "${SOURCE}" bytes)
if(NOT bytes EQUAL 1396)
  message(FATAL_ERROR "Original intersecting-rings PDF length changed: ${bytes}")
endif()
file(SHA256 "${SOURCE}" before)
if(NOT before STREQUAL PINNED_SHA256)
  message(FATAL_ERROR "Original intersecting-rings PDF SHA-256 mismatch")
endif()

execute_process(
  COMMAND "${ORACLE}" --external-tikz-wstar-pdf "${SOURCE}"
    --external-report "${REPORT}" --expected-sha256 "${PINNED_SHA256}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE output ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "Original PDFium vs detached C11 W* pixel oracle failed: ${rc}: ${err}")
endif()

file(READ "${REPORT}" json)
foreach(field IN ITEMS result document_id reference_oracle
    original_source_pdfium_oracle detached_c11_native_raster native_ir
    source_sha256)
  string(JSON ${field} GET "${json}" ${field})
endforeach()
foreach(field IN ITEMS source_and_graph_released_before_raster
    source_bytes_unchanged_after_reference_and_native
    pdfium_fallback_used repeat_rgba_identical)
  string(JSON ${field} GET "${json}" ${field})
endforeach()
foreach(field IN ITEMS page_width page_height source_painted_paths
    source_clip_count source_w_nonzero_clip_path_leaves
    source_wstar_evenodd_clip_path_leaves source_clip_intersection_nodes
    comparison_union_orange_pixels reference_orange_pixels
    native_evenodd_orange_pixels overlap_orange_pixels
    native_nonzero_orange_pixels evenodd_nonzero_diff_pixels
    masked_gross_error_pixels fixed_interior_goldens generated_fixture_count)
  string(JSON ${field} GET "${json}" ${field})
endforeach()
string(JSON comparison_scope GET "${json}" comparison_scope)
string(JSON full_page_native_raster GET "${json}" full_page_native_raster)
string(JSON mean GET "${json}" masked_mean_rgb_error)

if(NOT result STREQUAL "passed"
   OR NOT document_id STREQUAL "texample.intersecting-rings-v1"
   OR NOT reference_oracle STREQUAL "original-pdfium"
   OR NOT original_source_pdfium_oracle STREQUAL "passed"
   OR NOT detached_c11_native_raster STREQUAL "passed"
   OR NOT full_page_native_raster STREQUAL "rejected_unsupported_stroke_profile"
   OR NOT comparison_scope STREQUAL "orange-fill-mask-excluding-source-strokes"
   OR NOT native_ir STREQUAL "lowered"
   OR NOT source_sha256 STREQUAL PINNED_SHA256
   OR NOT source_and_graph_released_before_raster
   OR NOT source_bytes_unchanged_after_reference_and_native
   OR pdfium_fallback_used
   OR NOT repeat_rgba_identical
   OR page_width LESS 1 OR page_height LESS 1
   OR source_painted_paths LESS 1 OR source_painted_paths GREATER 8
   OR source_clip_count LESS 1 OR source_clip_count GREATER 8
   OR NOT source_w_nonzero_clip_path_leaves EQUAL 0
   OR NOT source_wstar_evenodd_clip_path_leaves EQUAL 1
   OR comparison_union_orange_pixels LESS 100
   OR reference_orange_pixels LESS 100 OR native_evenodd_orange_pixels LESS 100
   OR overlap_orange_pixels LESS 100
   OR native_nonzero_orange_pixels LESS 100
   OR evenodd_nonzero_diff_pixels LESS 100
   OR mean GREATER 0.25 OR masked_gross_error_pixels GREATER 8
   OR NOT fixed_interior_goldens EQUAL 3
   OR NOT generated_fixture_count EQUAL 0)
  message(FATAL_ERROR "Candidate W* report has unexpected semantics or pixel parity")
endif()
math(EXPR required_overlap "${reference_orange_pixels} * 99")
math(EXPR actual_overlap "${overlap_orange_pixels} * 100")
if(actual_overlap LESS required_overlap)
  message(FATAL_ERROR "Orange W* source/native fill-mask overlap is below 99%")
endif()

file(SHA256 "${SOURCE}" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "Original TeXample PDF bytes changed during pixel oracle")
endif()
message(STATUS "texample-wstar-original-pixel: passed; sha=${after} "
  "page=${page_width}x${page_height} Wstar=${source_wstar_evenodd_clip_path_leaves} "
  "orange=${reference_orange_pixels}/${native_evenodd_orange_pixels} "
  "evenodd-vs-nonzero-diff=${evenodd_nonzero_diff_pixels}")
