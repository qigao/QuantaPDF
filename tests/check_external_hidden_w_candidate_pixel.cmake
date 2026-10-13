# Unmodified independently published yWorks W n nested Clip:
# candidate-only original PDFium vs detached C11 pixels, never admitted.
cmake_minimum_required(VERSION 3.24)
foreach(arg IN ITEMS ORACLE SOURCE REPORT EXPECTED_SHA256)
  if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${arg}")
  endif()
endforeach()
file(SIZE "${SOURCE}" bytes)
if(NOT bytes EQUAL 3309)
  message(FATAL_ERROR "Changed hidden W original length")
endif()
file(SHA256 "${SOURCE}" before)
if(NOT before STREQUAL EXPECTED_SHA256)
  message(FATAL_ERROR "Original hidden W SHA-256 mismatch")
endif()
execute_process(
  COMMAND "${ORACLE}" --external-hidden-w-candidate-pdf "${SOURCE}"
    --external-report "${REPORT}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE output ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "Independent original PDFium/C11 hidden W candidate failed: ${rc}: ${err}")
endif()
file(READ "${REPORT}" json)
string(JSON result GET "${json}" result)
string(JSON oracle GET "${json}" original_source_pdfium_oracle)
string(JSON raster GET "${json}" detached_c11_native_raster)
string(JSON admitted GET "${json}" official_corpus_admitted)
string(JSON w GET "${json}" native_w_nonzero_clip_path_leaves)
string(JSON wstar GET "${json}" native_wstar_evenodd_clip_path_leaves)
string(JSON intersection GET "${json}" native_clip_intersection_nodes)
string(JSON repeat GET "${json}" repeat_rgba_identical)
string(JSON positives GET "${json}" external_wstar_pixel_supported_count)
string(JSON generated GET "${json}" generated_fixture_count)
if(NOT result STREQUAL "candidate_pixel_parity_passed_not_admitted"
   OR NOT oracle STREQUAL "passed" OR NOT raster STREQUAL "passed"
   OR admitted OR NOT w EQUAL 2 OR NOT wstar EQUAL 0
   OR NOT intersection EQUAL 1 OR NOT repeat
   OR NOT positives EQUAL 0 OR NOT generated EQUAL 0)
  message(FATAL_ERROR "Hidden W candidate pixel report contaminated official counts")
endif()
file(SHA256 "${SOURCE}" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "Original hidden W PDF bytes modified")
endif()
message(STATUS "candidate-hidden-W-pixel: passed; original=${after}; W=2 W*=0 admitted=0")
