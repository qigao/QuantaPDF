cmake_minimum_required(VERSION 3.20)

# #269 READ-ONLY quarantined external original: exact hash before/after
# existing real Source→Graph Snapshot→Owned IR typed preflight.
# This is observation-only; never a native pixel pass or admission count.
if(NOT DEFINED ORIGINAL_FILE OR NOT DEFINED ORACLE_EXECUTABLE OR
   NOT DEFINED REPORT_FILE OR NOT DEFINED PAGE_INDEX)
  message(FATAL_ERROR "Explicit original, observer, report and selected page required")
endif()

file(SIZE "${ORIGINAL_FILE}" size_before)
file(SHA256 "${ORIGINAL_FILE}" sha_before)
if(NOT size_before EQUAL 272039 OR
   NOT sha_before STREQUAL
     "cc195eac510b81123de4f55ac9f8185f2120975a7b75a106460b98615737aacd")
  message(FATAL_ERROR "Unmodified upstream py-pdf PDF source digest mismatch")
endif()
if(PAGE_INDEX LESS 0 OR PAGE_INDEX GREATER 3)
  message(FATAL_ERROR "Only four pinned source pages can be inspected")
endif()

file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(
  COMMAND "${ORACLE_EXECUTABLE}"
    --preflight-original-pdf "${ORIGINAL_FILE}"
    --page-index "${PAGE_INDEX}"
    --expect-status unsupported
    --preflight-report "${REPORT_FILE}"
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE stdout_text
  ERROR_VARIABLE stderr_text
  TIMEOUT 30)
if(NOT rc STREQUAL "0")
  message(FATAL_ERROR
    "Real selected-page typed preflight failed; source error is not "
    "successful UNSUPPORTED admission: rc=${rc}\n"
    "stdout=${stdout_text}\nstderr=${stderr_text}")
endif()

file(SIZE "${ORIGINAL_FILE}" size_after)
file(SHA256 "${ORIGINAL_FILE}" sha_after)
if(NOT size_after EQUAL size_before OR
   NOT sha_after STREQUAL sha_before)
  message(FATAL_ERROR "Original published PDF was changed during observation")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "Typed preflight did not publish a bounded report")
endif()
file(READ "${REPORT_FILE}" report_text)
foreach(field IN ITEMS
  "\"result\": \"typed_selected_page_preflight_only\""
  "\"original_source_provenance_verified\": false"
  "\"native_lower_status\": \"unsupported\""
  "\"detached_ir_pages\": 0"
  "\"detached_ir_operations\": 0"
  "\"detached_ir_clips\": 0"
  "\"detached_ir_graphics_states\": 0"
  "\"original_pdfium_pixel_oracle\": \"not_run\""
  "\"native_raster\": \"not_run\""
  "\"external_wstar_pixel_supported_count\": 0"
  "\"generated_fixture_count\": 0")
  string(FIND "${report_text}" "${field}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Missing fail-closed source-observation claim: ${field}")
  endif()
endforeach()
string(FIND "${report_text}" "\"selected_page_index\": ${PAGE_INDEX}" selected)
if(selected EQUAL -1)
  message(FATAL_ERROR "Selected original source page report mismatch")
endif()
if(NOT report_text MATCHES
  [=["native_lower_status": "unsupported"]=])
  message(FATAL_ERROR "Complex py-pdf page must reject atomically; native positive forbidden")
endif()

message(STATUS
  "pypdf original page ${PAGE_INDEX}: exact SHA256 preserved, "
  "typed=unsupported, output_resources=0, pdfium=not_run, C11Raster=not_run, "
  "external_native_positive_increment=0")
