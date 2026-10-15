cmake_minimum_required(VERSION 3.20)

# #269 diagnostic only. Distinguishes standard Graph capture budget from
# the actual selected-page Group/Content/Path lowering after successful
# isolated larger-but-bounded Graph capture. No native pixels are rendered.
if(NOT DEFINED ORIGINAL_FILE OR NOT DEFINED ORACLE_EXECUTABLE OR
   NOT DEFINED REPORT_FILE)
  message(FATAL_ERROR "Explicit original, observer and report required")
endif()
file(SIZE "${ORIGINAL_FILE}" before_size)
file(SHA256 "${ORIGINAL_FILE}" before_sha)
if(NOT before_size EQUAL 272039 OR NOT before_sha STREQUAL
  "cc195eac510b81123de4f55ac9f8185f2120975a7b75a106460b98615737aacd")
  message(FATAL_ERROR "External original changed before staged observation")
endif()
file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(
  COMMAND "${ORACLE_EXECUTABLE}"
    --quarantined-pypdf-capture-stage-matrix "${ORIGINAL_FILE}"
    --stage-report "${REPORT_FILE}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE stdout_text ERROR_VARIABLE stderr_text
  TIMEOUT 45)
if(NOT rc STREQUAL "0")
  message(FATAL_ERROR
    "Bounded original Graph stage classification failed, rc=${rc}\n"
    "stdout=${stdout_text}\nstderr=${stderr_text}")
endif()
file(SIZE "${ORIGINAL_FILE}" after_size)
file(SHA256 "${ORIGINAL_FILE}" after_sha)
if(NOT after_size EQUAL before_size OR NOT after_sha STREQUAL before_sha)
  message(FATAL_ERROR "External original mutated during stage classification")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "Missing sealed Graph stage classification report")
endif()
file(READ "${REPORT_FILE}" payload)
foreach(fragment IN ITEMS
  "\"source_kind\": \"independently_published_original_quarantine\""
  "\"default_Graph_capture\": \"unsupported\""
  "\"wide_Graph_capture\": \"ok\""
  "\"graph_owns_no_source_document\": true"
  "\"source_document_closed_before_page_stages\": true"
  "\"external_native_positive_increment\":0"
  "\"original_pdfium_pixel_oracle\":\"not_run\""
  "\"native_raster\":\"not_run\"")
  string(FIND "${payload}" "${fragment}" idx)
  if(idx EQUAL -1)
    message(FATAL_ERROR "Missing strict staged-original evidence ${fragment}")
  endif()
endforeach()
foreach(name IN ITEMS
  default wide only_nodes_default only_edges_default
  only_value_bytes_default only_stream_bytes_default
  only_total_storage_default)
  string(FIND "${payload}" "\"name\":\"${name}\"" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Missing budget trial ${name}")
  endif()
endforeach()
foreach(page RANGE 0 3)
  string(FIND "${payload}" "\"page_index\":${page}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Missing content-stage record for page ${page}")
  endif()
endforeach()
message(STATUS
  "Quarantined upstream original: default Graph capture UNSUPPORTED; "
  "wide bounded Graph capture OK; 7 budget trials + 4 separate "
  "Group/Content/Lowering observations; native/PDFium pixels NOT run")
