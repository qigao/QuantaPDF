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

# Surface a bounded number of source-page interior parity witnesses before
# freezing their immutable coordinates as reviewer-approved goldens.
# Preserve the complete RGB diagnostics; [^\\n] accidentally excludes the
# literal letter n and truncated all witness records at "refere".
string(REGEX MATCH "\\[texample-wstar-full-page\\] mean=[^\r\n]*" full_page_diagnostic "${err}")
if(full_page_diagnostic STREQUAL "")
  message(FATAL_ERROR "Missing whole-page source/PDFium stroke diagnostic")
endif()
message(STATUS "${full_page_diagnostic}")
string(REGEX MATCHALL "\\[texample-wstar-parity-interior\\][^\r\n]*" parity_witnesses "${err}")
list(LENGTH parity_witnesses parity_witness_count)
if(parity_witness_count LESS 3 OR parity_witness_count GREATER 8)
  message(FATAL_ERROR "Unexpected interior parity witness count: ${parity_witness_count}")
endif()
foreach(witness IN LISTS parity_witnesses)
  message(STATUS "${witness}")
endforeach()

file(READ "${REPORT}" json)
foreach(field IN ITEMS result document_id reference_oracle
    original_source_pdfium_oracle detached_c11_native_raster native_ir
    source_sha256)
  string(JSON ${field} GET "${json}" ${field})
endforeach()
foreach(field IN ITEMS source_and_graph_released_before_raster
    source_bytes_unchanged_after_reference_and_native
    pdfium_fallback_used repeat_rgba_identical
    official_corpus_admitted
    full_page_repeat_rgba_identical
    full_page_candidate_pixel_gate_passed)
  string(JSON ${field} GET "${json}" ${field})
endforeach()
foreach(field IN ITEMS page_width page_height source_painted_paths
    source_clip_count source_w_nonzero_clip_path_leaves
    source_wstar_evenodd_clip_path_leaves source_clip_intersection_nodes
    full_page_gross_error_pixels full_page_gray_reference_pixels
    full_page_gray_native_pixels full_page_gray_overlap_pixels
    full_page_fixed_gray_golden_count comparison_union_orange_pixels
    reference_orange_pixels native_evenodd_orange_pixels overlap_orange_pixels
    native_nonzero_orange_pixels evenodd_nonzero_diff_pixels
    masked_gross_error_pixels fixed_interior_goldens
    fixed_parity_exclusion_goldens
    external_wstar_pixel_supported_count generated_fixture_count)
  string(JSON ${field} GET "${json}" ${field})
endforeach()
string(JSON comparison_scope GET "${json}" comparison_scope)
string(JSON full_page_native_raster GET "${json}" full_page_native_raster)
string(JSON gray_original GET "${json}" full_page_gray_golden_original_rgb)
string(JSON gray_native GET "${json}" full_page_gray_golden_native_rgb)
string(JSON mean GET "${json}" masked_mean_rgb_error)
string(JSON full_page_mean GET "${json}" full_page_mean_rgb_error)

# A better stroke implementation may improve whole-page parity. The checker
# must not demand evidence of a mismatch (as the previous negative condition
# did): qualification scope is encoded by the report and the separate
# whole-page cross-platform gate, not by artificially requiring large error.
# Guard the measurement's *validity*, without claiming whole-page parity.
if(NOT full_page_mean MATCHES "^[0-9]+(\\.[0-9]+)?$")
  message(FATAL_ERROR "Nonfinite/malformed full-page mean RGB error: ${full_page_mean}")
endif()
math(EXPR full_page_pixels "${page_width} * ${page_height}")
message(STATUS "texample-wstar-whole-page-diagnostic: unqualified; "
  "mean-rgb=${full_page_mean} gross=${full_page_gross_error_pixels} "
  "page=${page_width}x${page_height}; W* fill-mask scored separately")

if(NOT result STREQUAL "passed"
   OR NOT document_id STREQUAL "texample.intersecting-rings-v1"
   OR NOT reference_oracle STREQUAL "original-pdfium"
   OR NOT original_source_pdfium_oracle STREQUAL "passed"
   OR NOT detached_c11_native_raster STREQUAL "wstar-fill-mask-only"
   OR NOT full_page_native_raster STREQUAL "candidate_whole_page_pixel_gate_passed_not_admitted"
   OR NOT full_page_repeat_rgba_identical
   OR NOT full_page_candidate_pixel_gate_passed
   OR full_page_mean LESS 0 OR full_page_mean GREATER 1.0
   OR NOT full_page_gross_error_pixels EQUAL 0
   OR full_page_gray_reference_pixels LESS 2500
   OR full_page_gray_native_pixels LESS 2500
   OR full_page_gray_overlap_pixels GREATER full_page_gray_reference_pixels
   OR NOT full_page_fixed_gray_golden_count EQUAL 1
   OR NOT gray_original STREQUAL "85,85,85"
   OR gray_native STREQUAL ""
   OR NOT page_width EQUAL 238 OR NOT page_height EQUAL 181
   OR NOT comparison_scope STREQUAL "original-whole-page-and-wstar-fill-mask-candidate-only"
   OR NOT native_ir STREQUAL "lowered"
   OR NOT source_sha256 STREQUAL PINNED_SHA256
   OR NOT source_and_graph_released_before_raster
   OR NOT source_bytes_unchanged_after_reference_and_native
   OR pdfium_fallback_used
   OR NOT repeat_rgba_identical
   OR official_corpus_admitted
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
   OR NOT fixed_parity_exclusion_goldens EQUAL 3
   OR NOT external_wstar_pixel_supported_count EQUAL 0
   OR NOT generated_fixture_count EQUAL 0)
  message(FATAL_ERROR "Candidate W* report has unexpected semantics or pixel parity")
endif()
math(EXPR gray_required_overlap "${full_page_gray_reference_pixels} * 94")
math(EXPR gray_actual_overlap "${full_page_gray_overlap_pixels} * 100")
if(gray_actual_overlap LESS gray_required_overlap)
  message(FATAL_ERROR "Original-page gray stroke overlap below 94%")
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
message(STATUS "texample-wstar-fill-mask-candidate-only: passed; sha=${after} "
  "page=${page_width}x${page_height} Wstar=${source_wstar_evenodd_clip_path_leaves} "
  "orange=${reference_orange_pixels}/${native_evenodd_orange_pixels} "
  "evenodd-vs-nonzero-diff=${evenodd_nonzero_diff_pixels}")
