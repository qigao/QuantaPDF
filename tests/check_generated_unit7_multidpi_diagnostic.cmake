cmake_minimum_required(VERSION 3.20)

# Only the historically qualified 72DPI pixels are an exact-RGB gate.
# 96DPI/144DPI results are published DIAGNOSTICS, not new positives.
if(NOT DEFINED ORACLE_EXECUTABLE OR ORACLE_EXECUTABLE STREQUAL "" OR
   NOT DEFINED REPORT_FILE OR REPORT_FILE STREQUAL "")
  message(FATAL_ERROR "Unit7 multidpi diagnostic executable and report required")
endif()
file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(
  COMMAND "${ORACLE_EXECUTABLE}"
    --generated-unit7-multidpi-original-diagnostic
    --multidpi-report "${REPORT_FILE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout_text
  ERROR_VARIABLE stderr_text
  TIMEOUT 35)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR
    "Generated original Unit7 multidpi diagnostic failed:\n"
    "stdout=${stdout_text}\nstderr=${stderr_text}\nrc=${result}")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "Multidpi diagnostic report not published")
endif()
file(READ "${REPORT_FILE}" report)
foreach(field IN ITEMS
  "\"source_kind\":\"test_generated_original_pdf\""
  "\"original_source_provider\":\"test_owned_qpdf_writer\""
  "\"page_user_unit\":7"
  "\"borrowed_ir_unchanged\":true"
  "\"source_byte_repeat_count\":4"
  "\"zero_output_refusals\":36"
  "\"dpi72_zero_rgb_required\":true"
  "\"dpi96_pixel_parity\":\"diagnostic_only\""
  "\"dpi144_pixel_parity\":\"diagnostic_only\""
  "\"external_original_provenance_verified\":false"
  "\"external_native_positive_increment\":0"
  "\"public_sdk_backend_switched\":false")
  string(FIND "${report}" "${field}" marker)
  if(marker EQUAL -1)
    message(FATAL_ERROR "Multidpi nonpromotion marker missing: ${field}")
  endif()
endforeach()
string(JSON original_count GET "${report}" source_originals)
string(JSON frame_count GET "${report}" frame_count)
string(JSON image_pixels GET "${report}" total_rgb_pixels)
string(JSON count LENGTH "${report}" frames)
if(NOT original_count EQUAL 4 OR NOT frame_count EQUAL 12 OR
   NOT count EQUAL 12 OR NOT image_pixels EQUAL 106600)
  message(FATAL_ERROR "Multidpi counts or total pixel count changed")
endif()
set(sum96 0)
set(sum144 0)
set(exact72 0)
foreach(index RANGE 0 11)
  string(JSON rotation GET "${report}" frames ${index} rotation)
  string(JSON dpi GET "${report}" frames ${index} dpi)
  string(JSON width GET "${report}" frames ${index} width)
  string(JSON height GET "${report}" frames ${index} height)
  string(JSON pixels GET "${report}" frames ${index} pixels)
  string(JSON abs_error GET "${report}" frames ${index} abs_rgb)
  string(JSON changed GET "${report}" frames ${index} different_pixels)
  string(JSON gross GET "${report}" frames ${index} gross_pixels)
  string(JSON maximum GET "${report}" frames ${index} max_channel_delta)
  math(EXPR rindex "${index} / 3")
  math(EXPR dindex "${index} % 3")
  math(EXPR expected_rotation "${rindex} * 90")
  if(NOT rotation EQUAL expected_rotation)
    message(FATAL_ERROR "Unexpected multidpi rotation order at index ${index}")
  endif()
  if(dindex EQUAL 0)
    set(w 70)
    set(h 56)
    set(expected_dpi 72)
  elseif(dindex EQUAL 1)
    set(w 94)
    set(h 75)
    set(expected_dpi 96)
  else()
    set(w 140)
    set(h 112)
    set(expected_dpi 144)
  endif()
  if(rotation EQUAL 90 OR rotation EQUAL 270)
    set(tmp "${w}")
    set(w "${h}")
    set(h "${tmp}")
  endif()
  math(EXPR expected_pixels "${w} * ${h}")
  if(NOT dpi EQUAL expected_dpi OR
     NOT width EQUAL w OR NOT height EQUAL h OR
     NOT pixels EQUAL expected_pixels OR
     changed GREATER pixels OR gross GREATER changed OR
     maximum GREATER 255)
    message(FATAL_ERROR "Corrupt multidpi pixel ledger at index ${index}")
  endif()
  if(dpi EQUAL 72)
    if(NOT abs_error EQUAL 0 OR NOT changed EQUAL 0 OR
       NOT gross EQUAL 0 OR NOT maximum EQUAL 0)
      message(FATAL_ERROR "Previously qualified Unit7 72DPI lost exact parity")
    endif()
    math(EXPR exact72 "${exact72}+1")
  elseif(dpi EQUAL 96)
    math(EXPR sum96 "${sum96}+${abs_error}")
  else()
    math(EXPR sum144 "${sum144}+${abs_error}")
  endif()
endforeach()
if(NOT exact72 EQUAL 4)
  message(FATAL_ERROR "4 strict Unit7 72DPI pages required")
endif()
string(JSON reported96 GET "${report}" dpi96_total_abs_rgb)
string(JSON reported144 GET "${report}" dpi144_total_abs_rgb)
if(NOT sum96 EQUAL reported96 OR NOT sum144 EQUAL reported144)
  message(FATAL_ERROR "Multidpi RGB totals disagree with frame rows")
endif()
message(STATUS
  "12 test-generated ORIGINAL unit7 frames; 72DPI 4/4 exact RGB; "
  "96DPI total absRGB=${sum96}, 144DPI total absRGB=${sum144} "
  "(both diagnostic, not external original positives)")
