cmake_minimum_required(VERSION 3.20)

# Independent original PDFium dimension oracle, NOT a C11 native pixel pass.
# The byte-pinned 4-page PDF contains unsupported Image/SMask/Text/Annots.
# Do not re-export Graph IR or silently substitute any native Raster path.
if(NOT DEFINED ORIGINAL_FILE OR NOT DEFINED ORACLE_EXECUTABLE OR
   NOT DEFINED REPORT_FILE)
  message(FATAL_ERROR "Original source, PDFium test oracle and report required")
endif()
file(SIZE "${ORIGINAL_FILE}" original_size)
file(SHA256 "${ORIGINAL_FILE}" original_sha)
if(NOT original_size EQUAL 272039 OR NOT original_sha STREQUAL
  "cc195eac510b81123de4f55ac9f8185f2120975a7b75a106460b98615737aacd")
  message(FATAL_ERROR "Original source hash or length changed")
endif()
file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(
  COMMAND "${ORACLE_EXECUTABLE}"
    --quarantined-pypdf-original-pdfium-dimensions "${ORIGINAL_FILE}"
    --pdfium-dimension-report "${REPORT_FILE}"
  RESULT_VARIABLE code OUTPUT_VARIABLE stdout_text ERROR_VARIABLE stderr_text
  TIMEOUT 60)
if(NOT code STREQUAL "0")
  message(FATAL_ERROR
    "Original PDFium image extent/byte-repeat failed (not C11 Raster): "
    "rc=${code}\nstdout=${stdout_text}\nstderr=${stderr_text}")
endif()
file(SIZE "${ORIGINAL_FILE}" after_size)
file(SHA256 "${ORIGINAL_FILE}" after_sha)
if(NOT after_size EQUAL original_size OR NOT after_sha STREQUAL original_sha)
  message(FATAL_ERROR "Original independent PDF mutated during PDFium qualification")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "Missing independent original PDFium extent report")
endif()
file(READ "${REPORT_FILE}" report)
foreach(field IN ITEMS
  "\"reference\":\"original_pdfium_rendering_only\""
  "\"page_mode_count\":12"
  "\"original_pdfium_device_dimensions_match_c11_grid\":true"
  "\"original_pdfium_byte_repeat\":true"
  "\"source_original_content_nonwhite\":true"
  "\"external_native_pixel_parity_qualified\":false"
  "\"native_raster\":\"not_run\""
  "\"external_native_positive_increment\":0")
  string(FIND "${report}" "${field}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Missing independent PDFium extent/non-positive field ${field}")
  endif()
endforeach()
foreach(page RANGE 0 3)
  foreach(dpi IN ITEMS 72 96 144)
    string(FIND "${report}" "\"page_index\":${page},\"dpi\":${dpi}" found)
    if(found EQUAL -1)
      message(FATAL_ERROR "Missing original PDFium page ${page} @ ${dpi}dpi")
    endif()
  endforeach()
endforeach()
string(REGEX MATCHALL [=["pdfium_nonwhite_pixels":[0-9]+]=] nonwhite "${report}")
list(LENGTH nonwhite count)
if(NOT count EQUAL 12)
  message(FATAL_ERROR "Expected 12 complete PDFium render checks, got ${count}")
endif()
message(STATUS
  "Original independent PDFium rendered 12 page/DPI combinations; "
  "all dimensions agree with C11 device geometry, native raster not run.")
