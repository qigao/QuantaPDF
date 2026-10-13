# Byte-identical external original PDF; QUARANTINED candidate-only
# PDFium-original vs pure-C11 6-Path 9-Clip full 500x800 pixel evidence.
cmake_minimum_required(VERSION 3.24)
foreach(arg IN ITEMS ORACLE SOURCE REPORT EXPECTED_SHA256)
  if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${arg}")
  endif()
endforeach()
file(SIZE "${SOURCE}" bytes)
if(NOT bytes EQUAL 4107)
  message(FATAL_ERROR "Original yWorks SVG W file length changed")
endif()
file(SHA256 "${SOURCE}" before)
if(NOT before STREQUAL EXPECTED_SHA256)
  message(FATAL_ERROR "Original SVG W SHA256 mismatch")
endif()
execute_process(
  COMMAND "${ORACLE}" --external-svg-w-candidate-pdf "${SOURCE}"
    --external-report "${REPORT}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE output ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "Original PDFium vs C11 SVG W pixel oracle failed: ${rc}: ${err}")
endif()
file(READ "${REPORT}" json)
string(JSON result GET "${json}" result)
string(JSON oracle GET "${json}" original_source_pdfium_oracle)
string(JSON native GET "${json}" detached_c11_native_raster)
string(JSON admitted GET "${json}" official_corpus_admitted)
string(JSON painted GET "${json}" native_painted_paths)
string(JSON w GET "${json}" native_w_nonzero_clip_path_leaves)
string(JSON wstar GET "${json}" native_wstar_evenodd_clip_path_leaves)
string(JSON intersects GET "${json}" native_clip_intersection_nodes)
string(JSON clips GET "${json}" native_total_clip_nodes)
string(JSON repeat GET "${json}" repeat_rgba_identical)
string(JSON positives GET "${json}" external_wstar_pixel_supported_count)
string(JSON generated GET "${json}" generated_fixture_count)
string(JSON mean GET "${json}" mean_rgb_error)
string(JSON gross GET "${json}" gross_error_pixels)
string(JSON ref_red GET "${json}" reference_red_pixels)
string(JSON native_red GET "${json}" native_red_pixels)
string(JSON overlap_red GET "${json}" overlap_red_pixels)
string(JSON goldens GET "${json}" fixed_interior_goldens)
if(NOT result STREQUAL "candidate_pixel_parity_passed_not_admitted"
   OR NOT oracle STREQUAL "passed" OR NOT native STREQUAL "passed"
   OR admitted OR NOT painted EQUAL 6 OR NOT w EQUAL 5
   OR NOT wstar EQUAL 0 OR NOT intersects EQUAL 4 OR NOT clips EQUAL 9
   OR NOT repeat OR NOT positives EQUAL 0 OR NOT generated EQUAL 0
   OR mean GREATER 0.05 OR NOT gross EQUAL 0
   OR NOT goldens EQUAL 7 OR ref_red LESS 5000
   OR native_red LESS 5000)
  message(FATAL_ERROR "SVG W candidate report contaminated official support")
endif()
math(EXPR required_red_overlap "${ref_red} * 99")
math(EXPR actual_red_overlap "${overlap_red} * 100")
if(actual_red_overlap LESS required_red_overlap)
  message(FATAL_ERROR "Original source red strokes mismatch beyond 1%")
endif()
file(SHA256 "${SOURCE}" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "Original external PDF mutated")
endif()
message(STATUS "svg-W-original-pixel: passed; sha=${after} W=5 W*=0 official_positive=0")
