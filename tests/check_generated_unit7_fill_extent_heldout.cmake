cmake_minimum_required(VERSION 3.20)

# Strictly validate bounded PDFium observations from 48 QPDF test-generated
# ORIGINAL Fill-only PDFs. No native Raster, no external source promotion.
if(NOT DEFINED ORACLE_EXECUTABLE OR ORACLE_EXECUTABLE STREQUAL "" OR
   NOT DEFINED REPORT_FILE OR REPORT_FILE STREQUAL "")
  message(FATAL_ERROR "Held-out original Fill source and report are required")
endif()
file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(
  COMMAND "${ORACLE_EXECUTABLE}"
    --generated-unit7-fill-extent-heldout
    --fill-extent-report "${REPORT_FILE}"
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  TIMEOUT 45)
if(NOT rc STREQUAL "0")
  message(FATAL_ERROR
    "Held-out real Fill-only source diagnostic failed:\n"
    "stdout=${out}\nstderr=${err}\nreturn code=${rc}")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "Held-out Fill-only source report missing")
endif()
file(READ "${REPORT_FILE}" report)
foreach(marker IN ITEMS
  "\"source_kind\":\"test_generated_original_pdf\""
  "\"source_producer\":\"test_owned_qpdf_writer\""
  "\"page_user_unit\":7"
  "\"dpi\":96"
  "\"source_files\":48"
  "\"source_byte_repeat_count\":48"
  "\"observed_device_pixels\":338400"
  "\"heldout_shape_count\":3"
  "\"crop_variant_count\":4"
  "\"quarter_rotation_count\":4"
  "\"frame_count\":48"
  "\"model_order\":\"fitted_outward,fitted_floor,fitted_floor_origin_ceil_extent,fitted_nearest_origin_ceil_extent,nominal_outward\""
  "\"pdfium_observation_only\":true"
  "\"native_raster\":\"not_run\""
  "\"external_original_provenance_verified\":false"
  "\"external_native_positive_increment\":0"
  "\"public_sdk_backend_switched\":false")
  string(FIND "${report}" "${marker}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Held-out original-source report missing: ${marker}")
  endif()
endforeach()
string(JSON row_count LENGTH "${report}" frames)
string(JSON match_length LENGTH "${report}" exact_integer_bbox_matches)
string(JSON total_length LENGTH "${report}" total_absolute_integer_edge_error)
if(NOT row_count EQUAL 48 OR
   NOT match_length EQUAL 5 OR NOT total_length EQUAL 5)
  message(FATAL_ERROR "Original-source extent matrix requires 48 rows and 5 finite models")
endif()

foreach(rule RANGE 0 4)
  set(match_${rule} 0)
  set(total_${rule} 0)
endforeach()
set(binary_count 0)
foreach(i RANGE 0 47)
  string(JSON shape GET "${report}" frames ${i} shape_id)
  string(JSON name GET "${report}" frames ${i} shape)
  string(JSON crop GET "${report}" frames ${i} crop_variant)
  string(JSON rotate GET "${report}" frames ${i} rotation)
  string(JSON w GET "${report}" frames ${i} width)
  string(JSON h GET "${report}" frames ${i} height)
  string(JSON red GET "${report}" frames ${i} red_pixels)
  string(JSON nonbinary GET "${report}" frames ${i} nonbinary_pixels)
  string(JSON rectangular GET "${report}" frames ${i} binary_rectangle)
  string(JSON observed_count LENGTH "${report}" frames ${i} observed_bbox)
  string(JSON bboxes_count LENGTH "${report}" frames ${i} model_bboxes)
  string(JSON errors_count LENGTH "${report}" frames ${i} model_abs_edge_error)
  math(EXPR expected_shape "${i}/16")
  math(EXPR expected_crop "(${i}%16)/4")
  math(EXPR expected_rotate "(${i}%4)*90")
  if(NOT shape EQUAL expected_shape OR
     NOT crop EQUAL expected_crop OR
     NOT rotate EQUAL expected_rotate OR
     NOT observed_count EQUAL 4 OR
     NOT bboxes_count EQUAL 5 OR NOT errors_count EQUAL 5)
    message(FATAL_ERROR "Held-out original source mode order or extent missing")
  endif()
  if(shape EQUAL 0)
    set(expected_name "five_by_four")
  elseif(shape EQUAL 1)
    set(expected_name "six_by_three")
  else()
    set(expected_name "fractional_five625_by_three375")
  endif()
  if(NOT name STREQUAL expected_name)
    message(FATAL_ERROR "Held-out source shape label does not match PDF content variant")
  endif()
  if(rotate EQUAL 0 OR rotate EQUAL 180)
    set(want_w 94)
    set(want_h 75)
  else()
    set(want_w 75)
    set(want_h 94)
  endif()
  math(EXPR pixels "${want_w}*${want_h}")
  if(NOT w EQUAL want_w OR NOT h EQUAL want_h OR
     red LESS 1 OR red GREATER pixels OR
     nonbinary GREATER pixels OR
     (NOT rectangular EQUAL 0 AND NOT rectangular EQUAL 1))
    message(FATAL_ERROR "Held-out original PDFium geometry or color count malformed")
  endif()
  foreach(side RANGE 0 3)
    string(JSON actual GET "${report}" frames ${i} observed_bbox ${side})
    if(side EQUAL 0)
      set(left "${actual}")
    elseif(side EQUAL 1)
      set(top "${actual}")
    elseif(side EQUAL 2)
      set(right "${actual}")
    else()
      set(bottom "${actual}")
    endif()
    if(side EQUAL 0 OR side EQUAL 2)
      set(limit "${w}")
    else()
      set(limit "${h}")
    endif()
    if(actual LESS 0 OR actual GREATER limit)
      message(FATAL_ERROR "Original PDFium red bbox exceeds bounded device")
    endif()
  endforeach()
  if(left GREATER_EQUAL right OR top GREATER_EQUAL bottom)
    message(FATAL_ERROR "Original PDFium red rectangle is empty")
  endif()
  math(EXPR area "(${right}-${left})*(${bottom}-${top})")
  if(red GREATER area OR
     (rectangular EQUAL 1 AND
      (NOT nonbinary EQUAL 0 OR NOT red EQUAL area)))
    message(FATAL_ERROR "Original PDFium red-mask rectangle flag inconsistent")
  endif()
  math(EXPR binary_count "${binary_count}+${rectangular}")
  foreach(rule RANGE 0 4)
    string(JSON edge_count LENGTH "${report}" frames ${i} model_bboxes ${rule})
    string(JSON reported GET "${report}" frames ${i} model_abs_edge_error ${rule})
    if(NOT edge_count EQUAL 4)
      message(FATAL_ERROR "A candidate needs exactly four integer bbox edges")
    endif()
    set(error 0)
    foreach(side RANGE 0 3)
      string(JSON expected GET "${report}" frames ${i} model_bboxes ${rule} ${side})
      string(JSON actual GET "${report}" frames ${i} observed_bbox ${side})
      if(side EQUAL 0 OR side EQUAL 2)
        set(limit "${w}")
      else()
        set(limit "${h}")
      endif()
      if(expected LESS 0 OR expected GREATER limit)
        message(FATAL_ERROR "Candidate integer bbox escapes caller capacity")
      endif()
      math(EXPR delta "${actual}-${expected}")
      if(delta LESS 0)
        math(EXPR delta "-${delta}")
      endif()
      math(EXPR error "${error}+${delta}")
    endforeach()
    if(NOT error EQUAL reported)
      message(FATAL_ERROR "Per-model observed original PDFium edge error wrong")
    endif()
    math(EXPR total_${rule} "${total_${rule}}+${error}")
    if(error EQUAL 0)
      math(EXPR match_${rule} "${match_${rule}}+1")
    endif()
  endforeach()
endforeach()
string(JSON recorded_binary GET "${report}" binary_red_rectangle_count)
if(NOT binary_count EQUAL recorded_binary)
  message(FATAL_ERROR "Held-out PDFium binary red-mask count does not reconcile")
endif()
foreach(rule RANGE 0 4)
  string(JSON reported_matches GET "${report}" exact_integer_bbox_matches ${rule})
  string(JSON reported_error GET "${report}" total_absolute_integer_edge_error ${rule})
  if(NOT reported_matches EQUAL match_${rule} OR
     NOT reported_error EQUAL total_${rule})
    message(FATAL_ERROR "Held-out PDFium original source A/B aggregate inconsistent")
  endif()
endforeach()
message(STATUS
  "Held-out Fill-only ORIGINAL source: 48 source PDF files, "
  "48 original PDFium repeat tests, bounded 338400 image pixels. "
  "PDFium binary rectangles=${binary_count}/48; "
  "integer models exact [${match_0},${match_1},${match_2},${match_3},${match_4}], "
  "edge abs [${total_0},${total_1},${total_2},${total_3},${total_4}]. "
  "DIAGNOSTIC ONLY; no native Raster or external-source promotion.")
