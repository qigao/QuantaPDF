# Exact-byte original W* candidate PDFium-vs-owned-pure-C11 pixel gate.
# This gate is experimental and must not increment admitted corpus counts.
cmake_minimum_required(VERSION 3.24)
foreach(arg IN ITEMS ORACLE SOURCE REPORT EXPECTED_SHA256)
  if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${arg}")
  endif()
endforeach()
file(SIZE "${SOURCE}" bytes)
if(NOT bytes EQUAL 3747)
  message(FATAL_ERROR "Original byte length mismatch")
endif()
file(SHA256 "${SOURCE}" before)
if(NOT "${before}" STREQUAL "${EXPECTED_SHA256}")
  message(FATAL_ERROR "Original PDF SHA-256 mismatch")
endif()
file(REMOVE "${REPORT}" "${REPORT}.tmp")
execute_process(
  COMMAND "${ORACLE}" --external-wstar-candidate-pixel-pdf "${SOURCE}"
      --external-report "${REPORT}"
  RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${code}" STREQUAL "0")
  message(FATAL_ERROR "Original PDFium vs C11 candidate pixel failed: ${code}: ${err}")
endif()
file(READ "${REPORT}" json)
string(JSON result GET "${json}" result)
string(JSON wstar GET "${json}" native_wstar_evenodd_clip_path_leaves)
string(JSON clips GET "${json}" native_clip_intersection_nodes)
string(JSON count GET "${json}" native_wstar_clip_supported_count)
string(JSON generated GET "${json}" generated_fixture_count)
string(JSON repeat GET "${json}" repeat_rgba_identical)
string(JSON oracle GET "${json}" original_source_pdfium_oracle)
string(JSON raster GET "${json}" detached_c11_native_raster)
if(NOT result STREQUAL "candidate_pixel_parity_passed_not_admitted"
   OR NOT wstar EQUAL 2 OR NOT clips EQUAL 3
   OR NOT count EQUAL 0 OR NOT generated EQUAL 0 OR NOT repeat
   OR NOT oracle STREQUAL "passed" OR NOT raster STREQUAL "passed")
  message(FATAL_ERROR "Pixel report modified candidate-only semantics")
endif()
file(SHA256 "${SOURCE}" after)
if(NOT "${before}" STREQUAL "${after}")
  message(FATAL_ERROR "Original PDF bytes mutated by pixel test")
endif()
message(STATUS "yworks original Wstar candidate independent PDFium/C11 pixel parity: passed; original=${after}; supported_count=0")
