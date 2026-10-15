cmake_minimum_required(VERSION 3.20)

# Test-only: capture the already-qualified generated ORIGINAL-PDF vs C11
# visible-IR raster metrics that ordinary passing CTest hides.
# This report is not a formal licensed-external PDF pixel-positive.
if(NOT DEFINED ORACLE_EXECUTABLE OR ORACLE_EXECUTABLE STREQUAL "" OR
   NOT DEFINED REPORT_FILE OR REPORT_FILE STREQUAL "")
  message(FATAL_ERROR "ORACLE_EXECUTABLE and REPORT_FILE are mandatory")
endif()

file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(
  COMMAND "${ORACLE_EXECUTABLE}" --generated-visible-page-pixels
  RESULT_VARIABLE exit_code
  OUTPUT_VARIABLE stdout_text
  ERROR_VARIABLE stderr_text
  TIMEOUT 30)
if(NOT exit_code STREQUAL "0")
  message(FATAL_ERROR
    "Original-PDFium/C11 source gate failed before report publication:\n"
    "${stdout_text}\n${stderr_text}")
endif()

string(REGEX MATCHALL
  [=[\[test-owned original-PDFium visible device\] rotation=[0-9]+ unit=[0-9]+ pixels=[0-9]+ abs_rgb=[0-9]+ differing=[0-9]+ gross=[0-9]+ max_delta=[0-9]+ mean=[0-9]+\.[0-9]+ NOT-formal-parity]=]
  rows "${stderr_text}")
list(LENGTH rows row_count)
if(NOT row_count EQUAL 8)
  message(FATAL_ERROR
    "Expected 8 original-PDFium RGB metrics, got ${row_count}:\n"
    "${stderr_text}")
endif()

set(expected_rotations 0 90 180 270)
set(json_rows "")
set(total_pixels 0)
set(total_abs 0)
set(total_different 0)
set(total_gross 0)
set(global_max 0)
set(index 0)
foreach(row IN LISTS rows)
  if(NOT row MATCHES
    [=[rotation=([0-9]+) unit=([0-9]+) pixels=([0-9]+) abs_rgb=([0-9]+) differing=([0-9]+) gross=([0-9]+) max_delta=([0-9]+) mean=([0-9]+\.[0-9]+) NOT-formal-parity]=])
    message(FATAL_ERROR "Unparseable original-PDFium metric: ${row}")
  endif()
  set(rotation "${CMAKE_MATCH_1}")
  set(unit "${CMAKE_MATCH_2}")
  set(pixels "${CMAKE_MATCH_3}")
  set(error "${CMAKE_MATCH_4}")
  set(different "${CMAKE_MATCH_5}")
  set(gross "${CMAKE_MATCH_6}")
  set(max_delta "${CMAKE_MATCH_7}")
  set(mean "${CMAKE_MATCH_8}")

  math(EXPR rotation_index "${index} / 2")
  math(EXPR unit_index "${index} % 2")
  list(GET expected_rotations ${rotation_index} expected_rotation)
  math(EXPR expected_unit "${unit_index} + 1")
  if(NOT rotation EQUAL expected_rotation OR
     NOT unit EQUAL expected_unit OR
     pixels LESS 1 OR different GREATER pixels OR
     gross GREATER different OR max_delta GREATER 255)
    message(FATAL_ERROR "Invalid or reordered original-PDFium metric: ${row}")
  endif()
  math(EXPR max_error "${pixels} * 765")
  if(error GREATER max_error)
    message(FATAL_ERROR "RGB error outside bounded page capacity: ${row}")
  endif()
  # #269 strict synthetic ORIGINAL-PDFIUM pixel parity: eight generated
  # PDFs are an exact *test-owned* gate after observation-only #334.
  # This does not qualify any independently published/licensed source.
  if(NOT error EQUAL 0 OR NOT different EQUAL 0 OR
     NOT gross EQUAL 0 OR NOT max_delta EQUAL 0 OR
     NOT mean STREQUAL "0.00000")
    message(FATAL_ERROR
      "Generated original-PDFium vs C11 exact RGB parity failed: ${row}")
  endif()

  if(index GREATER 0)
    string(APPEND json_rows ",\n")
  endif()
  string(APPEND json_rows
    "    {\"rotation\":${rotation},\"user_unit\":${unit},"
    "\"pixel_count\":${pixels},\"absolute_rgb_error\":${error},"
    "\"different_pixels\":${different},\"gross_pixels_gt_64\":${gross},"
    "\"max_channel_delta\":${max_delta},\"mean_rgb_error\":${mean}}")

  math(EXPR total_pixels "${total_pixels} + ${pixels}")
  math(EXPR total_abs "${total_abs} + ${error}")
  math(EXPR total_different "${total_different} + ${different}")
  math(EXPR total_gross "${total_gross} + ${gross}")
  if(max_delta GREATER global_max)
    set(global_max "${max_delta}")
  endif()
  math(EXPR index "${index} + 1")
endforeach()

set(document
  "{\n"
  "  \"schema_version\": 1,\n"
  "  \"source_kind\": \"test_generated_original_pdf\",\n"
  "  \"reference\": \"independent_original_pdfium\",\n"
  "  \"subject\": \"private_c11_visible_composer_fill_nonzero_w_clip\",\n"
  "  \"observation_only\": false,\n"
  "  \"generated_source_exact_rgb_parity\": true,\n"
  "  \"pixel_parity_qualified\": false,\n"
  "  \"external_source_positive_increment\": 0,\n"
  "  \"page_count\": 8,\n"
  "  \"pages\": [\n${json_rows}\n  ],\n"
  "  \"total_pixels\": ${total_pixels},\n"
  "  \"total_absolute_rgb_error\": ${total_abs},\n"
  "  \"total_different_pixels\": ${total_different},\n"
  "  \"total_gross_pixels_gt_64\": ${total_gross},\n"
  "  \"max_channel_delta\": ${global_max}\n"
  "}\n")
string(JOIN "" document_text ${document})
file(WRITE "${REPORT_FILE}.tmp" "${document_text}")
file(RENAME "${REPORT_FILE}.tmp" "${REPORT_FILE}")

message(STATUS
  "Generated original PDFium/C11: 8 pages, ${total_pixels} pixels, "
  "absRGB=${total_abs}, differing=${total_different}, gross=${total_gross}, "
  "maxDelta=${global_max}; NOT formal external pixel parity")
