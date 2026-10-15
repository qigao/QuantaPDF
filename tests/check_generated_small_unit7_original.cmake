cmake_minimum_required(VERSION 3.20)

# Actual small test-generated PDF original with nondefault /UserUnit 7,
# native W Clip+red Fill. This is NOT the unverified issue attachment and
# does NOT increment independent external-original pixel positives.
if(NOT DEFINED ORACLE_EXECUTABLE OR ORACLE_EXECUTABLE STREQUAL "" OR
   NOT DEFINED REPORT_FILE OR REPORT_FILE STREQUAL "")
  message(FATAL_ERROR "Unit7 original PDF oracle and explicit report required")
endif()
file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(
  COMMAND "${ORACLE_EXECUTABLE}"
    --generated-small-unit7-original-exact --unit7-report "${REPORT_FILE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout_text
  ERROR_VARIABLE stderr_text
  TIMEOUT 30)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR
    "Small generated UserUnit7 original PDFium/C11 zero-RGB gate failed:\n"
    "stdout=${stdout_text}\nstderr=${stderr_text}\nrc=${result}")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "Unit7 strict source-PDF RGB gate reported no output")
endif()
file(READ "${REPORT_FILE}" report)
foreach(field IN ITEMS
  "\"source_kind\":\"test_generated_original_pdf\""
  "\"page_user_unit\":7"
  "\"page_count\":4"
  "\"total_rgb_pixels\":15680"
  "\"generated_source_exact_rgb_parity\":true"
  "\"original_pdfium_byte_independent\":true"
  "\"external_original_provenance_verified\":false"
  "\"external_native_positive_increment\":0"
  "\"public_sdk_backend_switched\":false")
  string(FIND "${report}" "${field}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Missing original Unit7 exact-RGB safety marker: ${field}")
  endif()
endforeach()
foreach(rotation IN ITEMS 0 90 180 270)
  string(FIND "${report}" "\"rotation\":${rotation},\"user_unit\":7" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "Missing original generated Unit7 rotation ${rotation}")
  endif()
endforeach()
string(REGEX MATCHALL
  [=["abs_rgb":0,"different_pixels":0,"gross_pixels":0,"max_channel_delta":0]=]
  zero_rows "${report}")
list(LENGTH zero_rows n)
if(NOT n EQUAL 4)
  message(FATAL_ERROR
    "Expected exact zero RGB/diff/gross/max in all 4 generated Unit7 pages, got ${n}")
endif()
message(STATUS
  "4 generated ORIGINAL Unit7 W Clip + Fill pages, 15680 total pixels, "
  "0 absRGB, 0 different pixels, max channel 0; no external source admission")
