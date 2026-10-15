cmake_minimum_required(VERSION 3.20)

# An ORIGINAL independently published PDF is verified by the actual sealed
# Graph Query, separately from both lexical raw-page observation and strict
# unsupported painted-resource lowering. Native pixels are NOT rendered.
if(NOT DEFINED ORIGINAL_FILE OR NOT DEFINED ORACLE_EXECUTABLE OR
   NOT DEFINED REPORT_FILE)
  message(FATAL_ERROR "Explicit original source, observer and report required")
endif()
file(SIZE "${ORIGINAL_FILE}" before_size)
file(SHA256 "${ORIGINAL_FILE}" before_sha)
if(NOT before_size EQUAL 272039 OR NOT before_sha STREQUAL
   "cc195eac510b81123de4f55ac9f8185f2120975a7b75a106460b98615737aacd")
  message(FATAL_ERROR "Original external source byte identity changed")
endif()
file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(
  COMMAND "${ORACLE_EXECUTABLE}"
    --quarantined-pypdf-graph-geometry "${ORIGINAL_FILE}"
    --geometry-report "${REPORT_FILE}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE stdout_text ERROR_VARIABLE stderr_text
  TIMEOUT 30)
if(NOT rc STREQUAL "0")
  message(FATAL_ERROR
    "Real Source→Graph geometry failed: rc=${rc}\n"
    "stdout=${stdout_text}\nstderr=${stderr_text}")
endif()
file(SIZE "${ORIGINAL_FILE}" after_size)
file(SHA256 "${ORIGINAL_FILE}" after_sha)
if(NOT after_size EQUAL before_size OR NOT after_sha STREQUAL before_sha)
  message(FATAL_ERROR "Original PDF bytes modified during Graph observation")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "Selected-Graph geometry report missing")
endif()
file(READ "${REPORT_FILE}" report)
foreach(field IN ITEMS
  "\"graph_selected_page_geometry_verified\": true"
  "\"source_document_closed_before_geometry_report\": true"
  "\"graph_snapshot_released\": true"
  "\"user_unit_is_original_default_1\": true"
  "\"page_count\": 4"
  "\"original_pdfium_pixel_oracle\": \"not_run\""
  "\"native_raster\": \"not_run\""
  "\"external_native_positive_increment\": 0")
  string(FIND "${report}" "${field}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Missing strict Graph provenance/negative marker: ${field}")
  endif()
endforeach()
foreach(page RANGE 0 3)
  string(FIND "${report}" "\"page_index\": ${page}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "Graph report does not contain page ${page}")
  endif()
endforeach()

# Same ORIGINAL four-page CropBox/Rotate source, 3 DPI levels,
# 64 exact bounded inverse 4x4 device-subsample probes per mode.
# No PDFium original-pixel rendering or C11 paint admission.
foreach(field IN ITEMS
  "\"dpi_qualification_count\": 12"
  "\"total_source_roundtrip_samples\": 768")
  string(FIND "${report}" "${field}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Missing real-source multi-DPI bound: ${field}")
  endif()
endforeach()
foreach(page RANGE 0 3)
  foreach(dpi IN ITEMS 72 96 144)
    string(FIND "${report}" "\"page_index\":${page},\"dpi\":${dpi}" seen)
    if(seen EQUAL -1)
      message(FATAL_ERROR "Missing original page ${page} at ${dpi} DPI")
    endif()
  endforeach()
endforeach()
string(REGEX MATCHALL [=["source_roundtrip_samples":64]=] checks "${report}")
list(LENGTH checks roundtrip_rows)
if(NOT roundtrip_rows EQUAL 12)
  message(FATAL_ERROR "Require 12 x 64 exact inverse sample groups")
endif()
string(REGEX MATCHALL [=["limited_memory_refusal":true]=] refused "${report}")
list(LENGTH refused bounded_rows)
if(NOT bounded_rows EQUAL 12)
  message(FATAL_ERROR "Require exact pixel and byte cap refusals for each source mode")
endif()
message(STATUS
  "External pypdf original sealed Graph: four page display dimensions, "
  "CropBox/Rotate/default UserUnit validated; no C11 pixel admission")
