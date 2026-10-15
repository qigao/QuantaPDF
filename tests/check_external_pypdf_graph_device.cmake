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
  "\"capture_budget_only_node_limit_changed\": true"
  "\"default_node_limit\": 10000"
  "\"test_node_limit\": 16000"
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
# The node count must exceed the old cap while *every other*
# pre-existing limit is retained. This precisely attributes the old
# UNSUPPORTED to bounded Graph node admission, not Content decoding.
string(REGEX MATCH "\"graph_node_count\": ([0-9]+)" node_entry "${report}")
set(actual_nodes "${CMAKE_MATCH_1}")
string(REGEX MATCH "\"graph_edge_count\": ([0-9]+)" edge_entry "${report}")
set(actual_edges "${CMAKE_MATCH_1}")
string(REGEX MATCH "\"graph_model_bytes\": ([0-9]+)" byte_entry "${report}")
set(actual_bytes "${CMAKE_MATCH_1}")
string(REGEX MATCH "\"graph_owner_storage_bytes\": ([0-9]+)" store_entry "${report}")
set(actual_storage "${CMAKE_MATCH_1}")
if(NOT actual_nodes GREATER 10000 OR actual_nodes GREATER 16000 OR
   NOT actual_edges LESS_EQUAL 30000 OR
   NOT actual_bytes LESS_EQUAL 1048576 OR
   NOT actual_storage LESS_EQUAL 4194304)
  message(FATAL_ERROR
    "Pypdf Graph budget diagnosis not node-only: "
    "nodes=${actual_nodes}, edges=${actual_edges}, "
    "model_bytes=${actual_bytes}, storage=${actual_storage}")
endif()
foreach(page RANGE 0 3)
  string(FIND "${report}" "\"page_index\": ${page}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "Graph report does not contain page ${page}")
  endif()
endforeach()
message(STATUS
  "External pypdf original sealed Graph: four page display dimensions, "
  "CropBox/Rotate/default UserUnit validated; no C11 pixel admission")
