cmake_minimum_required(VERSION 3.20)

# Real test-generated SOURCE PDF CropBox perturbations at 96DPI.
# No third-party original provenance, native production cutover or fallback.
if(NOT DEFINED ORACLE_EXECUTABLE OR ORACLE_EXECUTABLE STREQUAL "" OR
   NOT DEFINED REPORT_FILE OR REPORT_FILE STREQUAL "")
  message(FATAL_ERROR "Explicit bounded crop-shift oracle/report required")
endif()
file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(
  COMMAND "${ORACLE_EXECUTABLE}"
    --generated-unit7-crop-shift-diagnostic --crop-shift-report "${REPORT_FILE}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE stdout_text
  ERROR_VARIABLE stderr_text TIMEOUT 35)
if(NOT rc STREQUAL "0")
  message(FATAL_ERROR
    "CropBox original-source matrix failed:\nstdout=${stdout_text}\n"
    "stderr=${stderr_text}\nrc=${rc}")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "No completed fractional CropBox JSON report")
endif()
file(READ "${REPORT_FILE}" report)
foreach(field IN ITEMS
  "\"source_kind\":\"test_generated_original_pdf\""
  "\"producer\":\"test_owned_qpdf_writer\""
  "\"test_owned_fractional_crop_shift\":true"
  "\"page_user_unit\":7"
  "\"dpi\":96"
  "\"shift_variant_count\":3"
  "\"rotations_per_variant\":4"
  "\"frame_count\":12"
  "\"source_byte_repeat_count\":12"
  "\"total_rgb_pixels\":84600"
  "\"external_original_provenance_verified\":false"
  "\"external_native_positive_increment\":0"
  "\"public_sdk_backend_switched\":false")
  string(FIND "${report}" "${field}" idx)
  if(idx EQUAL -1)
    message(FATAL_ERROR "Missing Source→Graph crop-shift nonpromotion marker: ${field}")
  endif()
endforeach()
string(JSON n LENGTH "${report}" frames)
if(NOT n EQUAL 12)
  message(FATAL_ERROR "Exactly twelve source-backed fractional-crop frames required")
endif()
set(sum_rgb 0)
set(rect_count 0)
foreach(index RANGE 0 11)
  string(JSON variant GET "${report}" frames ${index} variant)
  string(JSON label GET "${report}" frames ${index} variant_label)
  string(JSON rotation GET "${report}" frames ${index} rotation)
  string(JSON width GET "${report}" frames ${index} width)
  string(JSON height GET "${report}" frames ${index} height)
  string(JSON rgb GET "${report}" frames ${index} native_abs_rgb)
  string(JSON changed GET "${report}" frames ${index} different_pixels)
  string(JSON red GET "${report}" frames ${index} pdfium_red_pixels)
  string(JSON partial GET "${report}" frames ${index} pdfium_partial_pixels)
  string(JSON chroma GET "${report}" frames ${index} pdfium_unexpected_color_pixels)
  string(JSON binary GET "${report}" frames ${index} pdfium_binary_rectangle)
  string(JSON bbox_len LENGTH "${report}" frames ${index} pdfium_bbox)
  string(JSON steps_len LENGTH "${report}" frames ${index} edge_step_from_floor)
  if(NOT bbox_len EQUAL 4 OR NOT steps_len EQUAL 4)
    message(FATAL_ERROR "Four observed original rectangle edges required")
  endif()
  math(EXPR want_variant "(${index} / 4) + 1")
  math(EXPR want_rotation "(${index} % 4) * 90")
  if(NOT variant EQUAL want_variant OR NOT rotation EQUAL want_rotation)
    message(FATAL_ERROR "Unexpected source perturbation/rotation order")
  endif()
  if(variant EQUAL 1)
    set(want_label "x_plus_0_125")
  elseif(variant EQUAL 2)
    set(want_label "y_plus_0_125")
  else()
    set(want_label "xy_plus_0_375")
  endif()
  if(NOT label STREQUAL want_label)
    message(FATAL_ERROR "Unrecognized fractional CropBox source variant")
  endif()
  if(rotation EQUAL 0 OR rotation EQUAL 180)
    set(want_w 94)
    set(want_h 75)
  else()
    set(want_w 75)
    set(want_h 94)
  endif()
  math(EXPR pixels "${want_w} * ${want_h}")
  if(NOT width EQUAL want_w OR NOT height EQUAL want_h OR
     changed GREATER pixels OR red LESS 1 OR red GREATER pixels OR
     partial GREATER pixels OR chroma GREATER pixels OR
     (NOT binary EQUAL 0 AND NOT binary EQUAL 1))
    message(FATAL_ERROR "Invalid bounded original PDFium/source bitmap ledger")
  endif()
  foreach(side RANGE 0 3)
    string(JSON bound GET "${report}" frames ${index} pdfium_bbox ${side})
    string(JSON step GET "${report}" frames ${index} edge_step_from_floor ${side})
    if(step LESS -3 OR step GREATER 3)
      message(FATAL_ERROR "Observed PDFium integer edge step exceeds diagnostic range")
    endif()
    if(side EQUAL 0)
      set(left "${bound}")
    elseif(side EQUAL 1)
      set(top "${bound}")
    elseif(side EQUAL 2)
      set(right "${bound}")
    else()
      set(bottom "${bound}")
    endif()
  endforeach()
  if(left LESS 0 OR top LESS 0 OR right GREATER want_w OR
     bottom GREATER want_h OR left GREATER_EQUAL right OR
     top GREATER_EQUAL bottom)
    message(FATAL_ERROR "Original PDFium pixel bounds malformed")
  endif()
  math(EXPR bbox_area "(${right}-${left})*(${bottom}-${top})")
  if(red GREATER bbox_area OR
     (binary EQUAL 1 AND NOT red EQUAL bbox_area))
    message(FATAL_ERROR "Declared original red binary rectangle not supported by count")
  endif()
  math(EXPR sum_rgb "${sum_rgb}+${rgb}")
  math(EXPR rect_count "${rect_count}+${binary}")
endforeach()
string(JSON reported_rgb GET "${report}" total_native_pdfium_abs_rgb)
string(JSON reported_rect GET "${report}" pdfium_binary_rectangle_count)
if(NOT sum_rgb EQUAL reported_rgb OR NOT rect_count EQUAL reported_rect)
  message(FATAL_ERROR "Cropped-original 96DPI per-source aggregate reconciliation failed")
endif()
message(STATUS
  "12 test-generated original shifted CropBox pages; 4 rotations; "
  "exact source bytes unchanged; PDFium binary rectangles=${rect_count}/12; "
  "C11/PDFium absolute RGB=${sum_rgb} DIAGNOSTIC only")
