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
  "\"default_graph_capture_status\": \"unsupported\""
  "\"default_graph_capture_error_code\": 5"
  "\"expanded_test_only_capture_status\": \"ok\""
  "\"page_count\": 4"
  "\"original_pdfium_pixel_oracle\": \"not_run\""
  "\"native_raster\": \"not_run\""
  "\"external_native_positive_increment\": 0")
  string(FIND "${report}" "${field}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Missing strict Graph provenance/negative marker: ${field}")
  endif()
endforeach()
# Require actual bounded source Graph accounting in the observation;
# no hardcoded assumptions about which limit is exhausted across platforms.
foreach(field IN ITEMS graph_node_count graph_edge_count
    graph_model_bytes graph_owner_storage_bytes)
  if(NOT report MATCHES "\"${field}\": ([0-9]+)")
    message(FATAL_ERROR "Missing Graph budget metric: ${field}")
  endif()
  if(NOT CMAKE_MATCH_1 GREATER 0)
    message(FATAL_ERROR "Empty graph budget metric: ${field}")
  endif()
endforeach()
foreach(page RANGE 0 3)
  string(FIND "${report}" "\"page_index\": ${page}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "Graph report does not contain page ${page}")
  endif()
endforeach()
message(STATUS
  "External pypdf original sealed Graph: four page display dimensions, "
  "CropBox/Rotate/default UserUnit validated; no C11 pixel admission")
