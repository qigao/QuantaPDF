cmake_minimum_required(VERSION 3.20)
# #360: strict source-backed spelling/bit-pattern/device-size observation gate.
if(NOT DEFINED ORACLE_EXECUTABLE OR ORACLE_EXECUTABLE STREQUAL "" OR
   NOT DEFINED REPORT_FILE OR REPORT_FILE STREQUAL "")
  message(FATAL_ERROR "Explicit #360 literal-original oracle/report required")
endif()
file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(
  COMMAND "${ORACLE_EXECUTABLE}" --decimal360-source-spellings
    --source-spellings-report "${REPORT_FILE}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 42)
if(NOT rc STREQUAL "0")
  message(FATAL_ERROR "Source-backed #360 oracle FAILED (rc=${rc}):\n${stdout}\n${stderr}")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "#360 atomic original-PDF report absent")
endif()
file(READ "${REPORT_FILE}" report)
foreach(marker IN ITEMS
    "\"source_kind\":\"test_generated_original_pdf\""
    "\"origin\":\"literal_pdf_bytes_not_reexport\""
    "\"decimal_contract_test_only\":true"
    "\"native_render\":\"not_run\""
    "\"public_sdk_backend_switched\":false"
    "\"external_native_positive_increment\":0")
  string(FIND "${report}" "${marker}" idx)
  if(idx EQUAL -1)
    message(FATAL_ERROR "Missing source/nonpromotion marker: ${marker}")
  endif()
endforeach()
string(JSON n LENGTH "${report}" cases)
string(JSON source_count GET "${report}" original_source_count)
string(JSON spelling_count GET "${report}" source_spelling_matches)
string(JSON repeat_count GET "${report}" pdfium_bitmap_repeat_count)
string(JSON frame_count GET "${report}" frame_count)
string(JSON integral_count GET "${report}" integer_span_pdfium_matches)
string(JSON unsupported_count GET "${report}" unsupported_dpi_zero_refusals)
if(NOT n EQUAL 13 OR NOT source_count EQUAL 13 OR
   NOT spelling_count EQUAL 52 OR NOT repeat_count EQUAL 39 OR
   NOT frame_count EQUAL 39 OR NOT integral_count EQUAL 33 OR
   NOT unsupported_count EQUAL 26)
  message(FATAL_ERROR "#360 source/Graph/PDFium ledger missing cases")
endif()
set(dpis 72 96 144)
foreach(i RANGE 0 12)
  string(JSON nbytes GET "${report}" cases ${i} original_bytes)
  string(JSON fnv GET "${report}" cases ${i} source_fnv64)
  string(JSON crop GET "${report}" cases ${i} crop)
  string(JSON bitcount LENGTH "${report}" cases ${i} raw_crop_binary64)
  string(JSON frames LENGTH "${report}" cases ${i} frames)
  if(nbytes LESS 200 OR nbytes GREATER 65536 OR
     NOT frames EQUAL 3 OR NOT bitcount EQUAL 4 OR crop STREQUAL "")
    message(FATAL_ERROR "#360 incomplete source/Graph observation ${i}")
  endif()
  string(LENGTH "${fnv}" fnv_len)
  if(NOT fnv_len EQUAL 16 OR NOT fnv MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "#360 source byte identity fingerprint missing")
  endif()
  foreach(j RANGE 0 3)
    string(JSON bits GET "${report}" cases ${i} raw_crop_binary64 ${j})
    string(LENGTH "${bits}" bits_len)
    if(NOT bits_len EQUAL 16 OR NOT bits MATCHES "^[0-9a-f]+$")
      message(FATAL_ERROR "#360 raw binary64 endpoint evidence missing")
    endif()
  endforeach()
  foreach(j RANGE 0 2)
    list(GET dpis ${j} expected_dpi)
    string(JSON dpi GET "${report}" cases ${i} frames ${j} dpi)
    string(JSON ew GET "${report}" cases ${i} frames ${j} exact_grid 0)
    string(JSON eh GET "${report}" cases ${i} frames ${j} exact_grid 1)
    string(JSON pw GET "${report}" cases ${i} frames ${j} original_pdfium 0)
    string(JSON ph GET "${report}" cases ${i} frames ${j} original_pdfium 1)
    string(JSON gw GET "${report}" cases ${i} frames ${j} raw_graph_grid 0)
    string(JSON gh GET "${report}" cases ${i} frames ${j} raw_graph_grid 1)
    if(NOT dpi EQUAL expected_dpi OR
       ew LESS 1 OR eh LESS 1 OR
       ew GREATER 500 OR eh GREATER 500 OR
       pw LESS 1 OR ph LESS 1 OR gw LESS 1 OR gh LESS 1)
      message(FATAL_ERROR "#360 invalid original Source/Grid frame")
    endif()
    if(i LESS 11)
      string(JSON parity GET "${report}" cases ${i} frames ${j} pdfium_matches_exact)
      if(NOT parity)
        message(FATAL_ERROR "#360 exact decimal span differs from original PDFium")
      endif()
    endif()
  endforeach()
endforeach()
message(STATUS
  "#360 source-backed decimal: 13 immutable test-owned literal PDFs, "
  "52 original Graph numeric spellings, 39 repeated PDFium frames, "
  "33 exact-integer-size PDFium matches, 26 hard unsupported DPI refuses; "
  "no production parser or renderer mutation.")
