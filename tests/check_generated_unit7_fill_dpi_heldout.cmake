cmake_minimum_required(VERSION 3.20)

# Exactly the same original test-generated QPDF byte stream at 5 DPIs
# per original page. These are source-PDFium observations only.
if(NOT DEFINED ORACLE_EXECUTABLE OR ORACLE_EXECUTABLE STREQUAL "" OR
   NOT DEFINED REPORT_FILE OR REPORT_FILE STREQUAL "")
  message(FATAL_ERROR "Explicit source PDFium DPI oracle and output required")
endif()
file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(
  COMMAND "${ORACLE_EXECUTABLE}"
    --generated-unit7-fill-dpi-heldout
    --fill-dpi-report "${REPORT_FILE}"
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE stdout_text
  ERROR_VARIABLE stderr_text
  TIMEOUT 45)
if(NOT rc STREQUAL "0")
  message(FATAL_ERROR
    "Fixed ORIGINAL Fill source DPI test failed:\n"
    "stdout=${stdout_text}\nstderr=${stderr_text}\nrc=${rc}")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "Missing five-DPI original source JSON report")
endif()
file(READ "${REPORT_FILE}" report)
foreach(marker IN ITEMS
  "\"source_kind\":\"test_generated_original_pdf\""
  "\"source_producer\":\"test_owned_qpdf_writer\""
  "\"source_files\":24"
  "\"source_byte_repeat_count\":24"
  "\"dpi_count\":5"
  "\"shape_count\":3"
  "\"crop_variant_count\":2"
  "\"quarter_rotation_count\":4"
  "\"frame_count\":120"
  "\"total_original_pdfium_pixels\":1419168"
  "\"private_grid_admitted_dpi_acceptances\":72"
  "\"private_grid_unadmitted_dpi_refusals\":48"
  "\"heldout_grid_model_test_only\":true"
  "\"dpis_120_168_not_authorized\":true"
  "\"model_order\":\"fitted_outward,fitted_floor,fitted_floor_origin_ceil_extent,fitted_nearest_origin_ceil_extent,nominal_outward\""
  "\"pdfium_dpi_original_source_only\":true"
  "\"native_raster\":\"not_run\""
  "\"external_original_provenance_verified\":false"
  "\"external_native_positive_increment\":0"
  "\"public_sdk_backend_switched\":false")
  string(FIND "${report}" "${marker}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Missing original five-DPI/provenance guard: ${marker}")
  endif()
endforeach()

string(JSON count LENGTH "${report}" frames)
string(JSON dpi_count LENGTH "${report}" dpi_totals)
string(JSON dpi_order_length LENGTH "${report}" dpi_order)
if(NOT count EQUAL 120 OR NOT dpi_count EQUAL 5 OR
   NOT dpi_order_length EQUAL 5)
  message(FATAL_ERROR "Expected 120 original DPI frame rows and five DPI totals")
endif()
set(dpi_list 72 96 120 144 168)
set(horizontal_list 70 94 117 140 164)
set(vertical_list 56 75 94 112 131)
set(pixels_expected 94080 169200 263952 376320 515616)
foreach(slot RANGE 0 4)
  list(GET dpi_list ${slot} dpi)
  list(GET pixels_expected ${slot} budget_pixels)
  string(JSON observed_dpi GET "${report}" dpi_order ${slot})
  if(NOT observed_dpi EQUAL dpi)
    message(FATAL_ERROR "Unexpected original PDFium DPI order")
  endif()
  set(red_${slot} 0)
  set(partial_${slot} 0)
  set(other_${slot} 0)
  set(binary_${slot} 0)
  set(grid_mismatch_${slot} 0)
  set(pixels_${slot} 0)
  foreach(rule RANGE 0 4)
    set(matches_${slot}_${rule} 0)
    set(errors_${slot}_${rule} 0)
  endforeach()
endforeach()
foreach(index RANGE 0 119)
  string(JSON shape GET "${report}" frames ${index} shape_id)
  string(JSON name GET "${report}" frames ${index} shape)
  string(JSON crop GET "${report}" frames ${index} crop_variant)
  string(JSON rotation GET "${report}" frames ${index} rotation)
  string(JSON dpi GET "${report}" frames ${index} dpi)
  string(JSON width GET "${report}" frames ${index} width)
  string(JSON height GET "${report}" frames ${index} height)
  string(JSON red GET "${report}" frames ${index} red_pixels)
  string(JSON partial GET "${report}" frames ${index} partial_green_pixels)
  string(JSON other GET "${report}" frames ${index} non_redwhite_pixels)
  string(JSON rectangle GET "${report}" frames ${index} binary_rectangle)
  string(JSON private_width GET "${report}" frames ${index} private_grid_width)
  string(JSON private_height GET "${report}" frames ${index} private_grid_height)
  string(JSON grid_mismatch GET "${report}" frames ${index} private_grid_differs_from_original_pdfium)
  string(JSON box_n LENGTH "${report}" frames ${index} observed_bbox)
  string(JSON model_n LENGTH "${report}" frames ${index} model_bboxes)
  string(JSON err_n LENGTH "${report}" frames ${index} model_abs_edge_error)
  math(EXPR source_index "${index}/5")
  math(EXPR expected_shape "${source_index}/8")
  math(EXPR crop_slot "(${source_index}%8)/4")
  math(EXPR rot_index "${source_index}%4")
  math(EXPR dpi_slot "${index}%5")
  math(EXPR expected_rotation "${rot_index}*90")
  list(GET dpi_list ${dpi_slot} expected_dpi)
  list(GET horizontal_list ${dpi_slot} horizontal)
  list(GET vertical_list ${dpi_slot} vertical)
  if(expected_shape EQUAL 0)
    set(expected_name "source_six_by_four")
  elseif(expected_shape EQUAL 1)
    set(expected_name "heldout_five_by_four")
  else()
    set(expected_name "heldout_fractional_five625_by_three375")
  endif()
  if(crop_slot EQUAL 0)
    set(expected_crop 0)
  else()
    set(expected_crop 3)
  endif()
  if(rotation EQUAL 0 OR rotation EQUAL 180)
    set(expected_width "${horizontal}")
    set(expected_height "${vertical}")
  else()
    set(expected_width "${vertical}")
    set(expected_height "${horizontal}")
  endif()
  math(EXPR frame_pixels "${expected_width}*${expected_height}")
  if(NOT shape EQUAL expected_shape OR
     NOT name STREQUAL expected_name OR
     NOT crop EQUAL expected_crop OR
     NOT rotation EQUAL expected_rotation OR
     NOT dpi EQUAL expected_dpi OR
     NOT width EQUAL expected_width OR NOT height EQUAL expected_height OR
     NOT box_n EQUAL 4 OR NOT model_n EQUAL 5 OR NOT err_n EQUAL 5)
    message(FATAL_ERROR "Unexpected original fixed-source DPI mode at ${index}")
  endif()
  if(red LESS 1 OR red GREATER frame_pixels OR
     partial GREATER frame_pixels OR other GREATER frame_pixels OR
     (NOT rectangle EQUAL 0 AND NOT rectangle EQUAL 1) OR
     (NOT grid_mismatch EQUAL 0 AND NOT grid_mismatch EQUAL 1))
    message(FATAL_ERROR "Invalid original PDFium pixel/capacity ledger")
  endif()
  if(dpi EQUAL 120 OR dpi EQUAL 168)
    if(NOT private_width EQUAL 0 OR
       NOT private_height EQUAL 0 OR NOT grid_mismatch EQUAL 0)
      message(FATAL_ERROR "Unsupported DPI must not publish private grid dimensions")
    endif()
  else()
    math(EXPR difference_x "${private_width}-${width}")
    math(EXPR difference_y "${private_height}-${height}")
    if(difference_x LESS -1 OR difference_x GREATER 1 OR
       difference_y LESS -1 OR difference_y GREATER 1)
      message(FATAL_ERROR "Private admitted grid deviates more than one pixel from original")
    endif()
    if((difference_x EQUAL 0 AND difference_y EQUAL 0 AND
        NOT grid_mismatch EQUAL 0) OR
       ((NOT difference_x EQUAL 0 OR NOT difference_y EQUAL 0) AND
        NOT grid_mismatch EQUAL 1))
      message(FATAL_ERROR "Private grid-vs-PDFium dimensions were misreported")
    endif()
  endif()
  math(EXPR grid_mismatch_${dpi_slot}
    "${grid_mismatch_${dpi_slot}}+${grid_mismatch}")
  set(bbox_edges)
  foreach(edge RANGE 0 3)
    string(JSON v GET "${report}" frames ${index} observed_bbox ${edge})
    list(APPEND bbox_edges "${v}")
    if(edge EQUAL 0 OR edge EQUAL 2)
      set(limit "${width}")
    else()
      set(limit "${height}")
    endif()
    if(v LESS 0 OR v GREATER limit)
      message(FATAL_ERROR "Original PDFium red bounds out of device size")
    endif()
  endforeach()
  list(GET bbox_edges 0 left)
  list(GET bbox_edges 1 top)
  list(GET bbox_edges 2 right)
  list(GET bbox_edges 3 bottom)
  if(left GREATER_EQUAL right OR top GREATER_EQUAL bottom)
    message(FATAL_ERROR "Original PDFium source red image has empty extent")
  endif()
  math(EXPR bbox_area "(${right}-${left})*(${bottom}-${top})")
  if(red GREATER bbox_area OR
     (rectangle EQUAL 1 AND
      (NOT red EQUAL bbox_area OR NOT other EQUAL 0)))
    message(FATAL_ERROR "Original PDFium binary rectangle marker is wrong")
  endif()
  math(EXPR pixels_${dpi_slot} "${pixels_${dpi_slot}}+${frame_pixels}")
  math(EXPR red_${dpi_slot} "${red_${dpi_slot}}+${red}")
  math(EXPR partial_${dpi_slot} "${partial_${dpi_slot}}+${partial}")
  math(EXPR other_${dpi_slot} "${other_${dpi_slot}}+${other}")
  math(EXPR binary_${dpi_slot} "${binary_${dpi_slot}}+${rectangle}")
  foreach(rule RANGE 0 4)
    string(JSON candidate_n LENGTH "${report}" frames ${index} model_bboxes ${rule})
    string(JSON reported_error GET "${report}" frames ${index} model_abs_edge_error ${rule})
    if(NOT candidate_n EQUAL 4)
      message(FATAL_ERROR "Five candidate source bboxes each require 4 integer edges")
    endif()
    set(absolute_error 0)
    foreach(edge RANGE 0 3)
      string(JSON predicted GET "${report}" frames ${index} model_bboxes ${rule} ${edge})
      list(GET bbox_edges ${edge} actual)
      if(edge EQUAL 0 OR edge EQUAL 2)
        set(limit "${width}")
      else()
        set(limit "${height}")
      endif()
      if(predicted LESS 0 OR predicted GREATER limit)
        message(FATAL_ERROR "Candidate source edge extends beyond supported grid")
      endif()
      math(EXPR distance "${actual}-${predicted}")
      if(distance LESS 0)
        math(EXPR distance "-${distance}")
      endif()
      math(EXPR absolute_error "${absolute_error}+${distance}")
    endforeach()
    if(NOT absolute_error EQUAL reported_error)
      message(FATAL_ERROR "Original PDFium candidate bbox error inconsistent")
    endif()
    math(EXPR errors_${dpi_slot}_${rule}
      "${errors_${dpi_slot}_${rule}}+${absolute_error}")
    if(absolute_error EQUAL 0)
      math(EXPR matches_${dpi_slot}_${rule}
        "${matches_${dpi_slot}_${rule}}+1")
    endif()
  endforeach()
endforeach()

set(all_pixels 0)
foreach(slot RANGE 0 4)
  list(GET dpi_list ${slot} expected_dpi)
  list(GET pixels_expected ${slot} expected_pixels)
  string(JSON dpi GET "${report}" dpi_totals ${slot} dpi)
  string(JSON pixels GET "${report}" dpi_totals ${slot} pixels)
  string(JSON red GET "${report}" dpi_totals ${slot} red_pixels)
  string(JSON partial GET "${report}" dpi_totals ${slot} partial_green_pixels)
  string(JSON other GET "${report}" dpi_totals ${slot} non_redwhite_pixels)
  string(JSON binary GET "${report}" dpi_totals ${slot} binary_rectangles)
  string(JSON private_mismatches GET "${report}" dpi_totals ${slot} private_grid_original_dimension_mismatches)
  string(JSON matches_length LENGTH "${report}" dpi_totals ${slot} model_exact_matches)
  string(JSON errors_length LENGTH "${report}" dpi_totals ${slot} model_abs_edge_errors)
  if(NOT dpi EQUAL expected_dpi OR
     NOT pixels EQUAL expected_pixels OR
     NOT pixels EQUAL pixels_${slot} OR
     NOT red EQUAL red_${slot} OR
     NOT partial EQUAL partial_${slot} OR
     NOT other EQUAL other_${slot} OR
     NOT binary EQUAL binary_${slot} OR
     NOT private_mismatches EQUAL grid_mismatch_${slot} OR
     NOT matches_length EQUAL 5 OR NOT errors_length EQUAL 5)
    message(FATAL_ERROR "Original PDFium scale-level ledger differs from frames")
  endif()
  foreach(rule RANGE 0 4)
    string(JSON reported_match GET "${report}" dpi_totals ${slot} model_exact_matches ${rule})
    string(JSON reported_error GET "${report}" dpi_totals ${slot} model_abs_edge_errors ${rule})
    if(NOT reported_match EQUAL matches_${slot}_${rule} OR
       NOT reported_error EQUAL errors_${slot}_${rule})
      message(FATAL_ERROR "Per-DPI numerical original Fill hypothesis mismatch")
    endif()
  endforeach()
  math(EXPR all_pixels "${all_pixels}+${pixels}")
endforeach()
if(NOT all_pixels EQUAL 1419168)
  message(FATAL_ERROR "Original PDFium total benchmark pixel budget changed")
endif()
set(all_grid_mismatches 0)
foreach(slot RANGE 0 4)
  math(EXPR all_grid_mismatches
    "${all_grid_mismatches}+${grid_mismatch_${slot}}")
endforeach()
string(JSON reported_grid_mismatches GET "${report}" private_grid_vs_pdfium_dimension_mismatch_cases)
if(NOT all_grid_mismatches EQUAL reported_grid_mismatches)
  message(FATAL_ERROR "Original PDFium vs private v0 DPI-grid discrepancy ledger differs")
endif()
message(STATUS
  "24 immutable QPDF test-generated originals at DPI 72/96/120/144/168 "
  "=> 120 repeated original PDFium source renders / 1419168 pixels. "
  "Grid v0 admitted 72 source-DPI modes, rejected 48 unsupported modes, "
  "with ${all_grid_mismatches} dimension discrepancies vs independent PDFium. "
  "Numerical candidate diagnostics per DPI, no C11 raster, "
  "no third-party source promotion and no SDK switch.")
