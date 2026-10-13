# Test-only atomic report preservation; never a PDF conformance count.
cmake_minimum_required(VERSION 3.24)
foreach(var IN ITEMS ORACLE W_SOURCE WSTAR_SOURCE OUTPUT_DIR)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${var}")
  endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(report "${OUTPUT_DIR}/typed-observation.json")
set(stage "${report}.tmp")
set(invalid "${OUTPUT_DIR}/invalid-pdf.pdf")
set(sentinel "previous-report-must-survive-failed-preflight\n")
file(SHA256 "${W_SOURCE}" before_w)
file(SHA256 "${WSTAR_SOURCE}" before_wstar)

function(expect_failed_preserves original status code reason)
  file(WRITE "${report}" "${sentinel}")
  file(REMOVE "${stage}")
  execute_process(
    COMMAND "${ORACLE}" --preflight-original-pdf "${original}"
            --page-index 0 --expect-status "${status}"
            --preflight-report "${report}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE ignored ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "${code}")
    message(FATAL_ERROR "Wrong preflight failure class for ${status}: expected ${code}/${reason}; actual=${rc}; stderr=${err}")
  endif()
  string(FIND "${err}" "[source-preflight-error] class=${reason}" reason_at)
  if(reason_at LESS 0)
    message(FATAL_ERROR "No explicit typed preflight rejection reason: ${err}")
  endif()
  if(NOT EXISTS "${report}")
    message(FATAL_ERROR "Failed typed preflight deleted a previous report")
  endif()
  file(READ "${report}" contents)
  if(NOT contents STREQUAL sentinel OR EXISTS "${stage}")
    message(FATAL_ERROR "Failed typed preflight corrupted prior evidence")
  endif()
endfunction()

function(expect_success_replaces original expected)
  file(WRITE "${report}" "${sentinel}")
  file(REMOVE "${stage}")
  execute_process(
    COMMAND "${ORACLE}" --preflight-original-pdf "${original}"
            --page-index 0 --expect-status observe
            --preflight-report "${report}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE ignored ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "Typed observe failed: ${rc}: ${err}")
  endif()
  file(READ "${report}" contents)
  foreach(fragment IN ITEMS
          "\"observation_only\": true"
          "\"native_lower_status\": \"${expected}\""
          "\"original_pdfium_pixel_oracle\": \"not_run\""
          "\"native_raster\": \"not_run\""
          "\"external_wstar_pixel_supported_count\": 0")
    string(FIND "${contents}" "${fragment}" at)
    if(at LESS 0)
      message(FATAL_ERROR "Typed observe report missing required field: ${fragment}")
    endif()
  endforeach()
  if(EXISTS "${stage}")
    message(FATAL_ERROR "Typed observe left a staged report")
  endif()
endfunction()

expect_failed_preserves("${W_SOURCE}" unsupported 71 status_mismatch)
expect_failed_preserves("${WSTAR_SOURCE}" lowered 71 status_mismatch)
file(WRITE "${invalid}" "%PDF-1.7\nnot a valid original pdf\n")
expect_failed_preserves("${invalid}" observe 70 source_error)
expect_success_replaces("${W_SOURCE}" lowered)
expect_success_replaces("${WSTAR_SOURCE}" unsupported)

file(SHA256 "${W_SOURCE}" after_w)
file(SHA256 "${WSTAR_SOURCE}" after_wstar)
if(NOT before_w STREQUAL after_w OR NOT before_wstar STREQUAL after_wstar)
  message(FATAL_ERROR "Pinned original PDF source bytes were modified")
endif()
message(STATUS "Typed report guard: 3 failure preservations, 2 replacements; W* pixels=0")
