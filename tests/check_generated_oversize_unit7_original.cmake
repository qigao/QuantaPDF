cmake_minimum_required(VERSION 3.20)

# Test-owned real PDF original, NOT the independent issue attachment.
# Reject the oversized Graph-selected /UserUnit 7 bitmap before allocation.
if(NOT DEFINED ORACLE_EXECUTABLE OR ORACLE_EXECUTABLE STREQUAL "" OR
   NOT DEFINED REPORT_FILE OR REPORT_FILE STREQUAL "")
  message(FATAL_ERROR "Oversize unit7 source oracle and explicit report required")
endif()
file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(
  COMMAND "${ORACLE_EXECUTABLE}"
    --generated-oversize-unit7-original-refusal
    --oversize-unit7-report "${REPORT_FILE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout_text
  ERROR_VARIABLE stderr_text
  TIMEOUT 30)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR
    "Oversize generated original /UserUnit7 Graph rejection failed:\n"
    "stdout=${stdout_text}\nstderr=${stderr_text}\nrc=${result}")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "Oversize original page report missing")
endif()
file(READ "${REPORT_FILE}" report)
foreach(field IN ITEMS
  "\"source_kind\":\"test_generated_original_pdf\""
  "\"page_user_unit\":7"
  "\"source_graph_geometry_verified\":true"
  "\"source_bytes_unchanged_cases\":4"
  "\"rotation_count\":4"
  "\"dpi_count\":3"
  "\"device_pixel_budget_refusals\":12"
  "\"rgb_byte_budget_refusals\":4"
  "\"q4_work_budget_refusals\":4"
  "\"device_bitmap_allocated\":false"
  "\"original_pdfium_pixel_oracle\":\"not_run\""
  "\"native_raster\":\"not_run\""
  "\"independent_reporter_attachment_verified\":false"
  "\"external_native_positive_increment\":0"
  "\"public_sdk_backend_switched\":false")
  string(FIND "${report}" "${field}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Oversize generated original safety marker missing: ${field}")
  endif()
endforeach()
message(STATUS
  "4 generated ORIGINAL unit7 source PDFs: 12/12 pixel, 4/4 byte, "
  "4/4 Q4 work preallocation refusals; no pixel allocation or external source positive")
