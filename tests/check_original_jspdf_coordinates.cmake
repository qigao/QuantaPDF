cmake_minimum_required(VERSION 3.24)
foreach(arg IN ITEMS ORACLE SOURCE)
  if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${arg}")
  endif()
endforeach()
if(IS_SYMLINK "${SOURCE}")
  message(FATAL_ERROR "Original source must not be a symlink")
endif()
file(SIZE "${SOURCE}" size)
file(SHA256 "${SOURCE}" before)
if(NOT size EQUAL 3344 OR NOT before STREQUAL
    "179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e")
  message(FATAL_ERROR "Immutable jsPDF original does not match")
endif()
execute_process(COMMAND "${ORACLE}" "${SOURCE}" TIMEOUT 10
  RESULT_VARIABLE rc OUTPUT_VARIABLE json ERROR_VARIABLE stderr)
file(SHA256 "${SOURCE}" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "Original jsPDF source changed")
endif()
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "Original coordinate API oracle failed: ${rc}: ${stderr}")
endif()
foreach(field IN ITEMS result coordinate_cases point_checks repeats numeric_repeat_equal
    pixel_center_observations raster_invocations native_pixel_positive_count
    official_corpus_admitted public_requested_rotation_qualified)
  string(JSON ${field} GET "${json}" ${field})
endforeach()
if(NOT result STREQUAL "original_pdfium_coordinate_api_passed"
    OR NOT coordinate_cases EQUAL 18 OR NOT point_checks EQUAL 486
    OR NOT repeats EQUAL 3 OR NOT numeric_repeat_equal
    OR NOT pixel_center_observations EQUAL 0 OR NOT raster_invocations EQUAL 0
    OR NOT native_pixel_positive_count EQUAL 0 OR official_corpus_admitted
    OR public_requested_rotation_qualified)
  message(FATAL_ERROR "Coordinate evidence was incomplete or overstated")
endif()
# Captured in CTest's log. No file output, report path alias, or stale report.
message(STATUS "original-jsPDF-coordinate-api: source_sha256=${after}\n${json}")
