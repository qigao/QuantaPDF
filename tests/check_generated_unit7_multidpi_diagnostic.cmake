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
  "\"analytic_coverage_test_only\":true"
  "\"quantum_ledger_diagnostic_only\":true"
  "\"integer_snap_test_only\":true"
  "\"integer_snap_policy_order\":\"nearest,floor,ceil,outward,inward\""
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
set(fit96 0)
set(nominal96 0)
set(far96 0)
set(q4native96 0)
set(q4pdfium96 0)
set(q4quantum96 0)
set(q4pixels96 0)
set(partialref96 0)
set(partialnative96 0)
set(refmore96 0)
set(nativemore96 0)
set(chroma96 0)
set(changed96 0)
foreach(edge RANGE 0 4)
  set(edgesum_${edge} 0)
  set(snap_abs_${edge} 0)
  set(snap_different_${edge} 0)
endforeach()
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
  string(JSON fitted_area GET "${report}" frames ${index} fitted_area_abs_rgb)
  string(JSON nominal_area GET "${report}" frames ${index} nominal_area_abs_rgb)
  string(JSON off_edge GET "${report}" frames ${index} off_ideal_edge_differences)
  string(JSON q4native GET "${report}" frames ${index} q4_analytic_native_abs_rgb)
  string(JSON q4pdfium GET "${report}" frames ${index} q4_analytic_pdfium_abs_rgb)
  string(JSON quantization GET "${report}" frames ${index} pdfium_nearest_q4_green_distance)
  string(JSON q4pixels GET "${report}" frames ${index} q4_analytic_native_diff_pixels)
  string(JSON pdfium_partial GET "${report}" frames ${index} pdfium_partial_green_pixels)
  string(JSON native_partial GET "${report}" frames ${index} native_partial_green_pixels)
  string(JSON refmore GET "${report}" frames ${index} pdfium_more_red_pixels)
  string(JSON nativemore GET "${report}" frames ${index} native_more_red_pixels)
  string(JSON chroma GET "${report}" frames ${index} pdfium_chroma_mismatch_pixels)
  string(JSON edgelength LENGTH "${report}" frames ${index} edge_diff_pixels)
  string(JSON snap_abs_length LENGTH "${report}" frames ${index} integer_snap_abs_rgb)
  string(JSON snap_diff_length LENGTH "${report}" frames ${index} integer_snap_differing_pixels)
  if(NOT edgelength EQUAL 5 OR
     NOT snap_abs_length EQUAL 5 OR NOT snap_diff_length EQUAL 5)
    message(FATAL_ERROR "Expected five disjoint edge and candidate snap bins")
  endif()
  set(frame_edges 0)
  foreach(edge RANGE 0 4)
    string(JSON edgecount GET "${report}" frames ${index} edge_diff_pixels ${edge})
    string(JSON snaperror GET "${report}" frames ${index} integer_snap_abs_rgb ${edge})
    string(JSON snapchanged GET "${report}" frames ${index} integer_snap_differing_pixels ${edge})
    math(EXPR frame_edges "${frame_edges}+${edgecount}")
    if(dpi EQUAL 96)
      math(EXPR edgesum_${edge} "${edgesum_${edge}}+${edgecount}")
      math(EXPR snap_abs_${edge} "${snap_abs_${edge}}+${snaperror}")
      math(EXPR snap_different_${edge} "${snap_different_${edge}}+${snapchanged}")
      math(EXPR rgb_byte_error_limit "${pixels} * 765")
      if(snapchanged GREATER pixels OR snaperror GREATER rgb_byte_error_limit)
        message(FATAL_ERROR "Snap candidate error budget cannot exceed RGB extent")
      endif()
    elseif(NOT edgecount EQUAL 0 OR
           NOT snaperror EQUAL 0 OR NOT snapchanged EQUAL 0)
      message(FATAL_ERROR "Non-96DPI edge/snap ledger unexpectedly enabled")
    endif()
  endforeach()
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
     off_edge GREATER changed OR maximum GREATER 255)
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
    math(EXPR fit96 "${fit96}+${fitted_area}")
    math(EXPR nominal96 "${nominal96}+${nominal_area}")
    math(EXPR far96 "${far96}+${off_edge}")
    math(EXPR q4native96 "${q4native96}+${q4native}")
    math(EXPR q4pdfium96 "${q4pdfium96}+${q4pdfium}")
    math(EXPR q4quantum96 "${q4quantum96}+${quantization}")
    math(EXPR q4pixels96 "${q4pixels96}+${q4pixels}")
    math(EXPR partialref96 "${partialref96}+${pdfium_partial}")
    math(EXPR partialnative96 "${partialnative96}+${native_partial}")
    math(EXPR refmore96 "${refmore96}+${refmore}")
    math(EXPR nativemore96 "${nativemore96}+${nativemore}")
    math(EXPR chroma96 "${chroma96}+${chroma}")
    math(EXPR changed96 "${changed96}+${changed}")
    math(EXPR grouped "${frame_edges}+${off_edge}")
    if(NOT grouped EQUAL changed OR
       q4pixels GREATER pixels OR pdfium_partial GREATER pixels OR
       native_partial GREATER pixels OR chroma GREATER pixels OR
       refmore GREATER changed OR nativemore GREATER changed)
      message(FATAL_ERROR "96DPI disjoint edge/quantization ledger is inconsistent")
    endif()
  else()
    if(NOT frame_edges EQUAL 0 OR q4native GREATER 0 OR
       q4pdfium GREATER 0 OR quantization GREATER 0 OR
       q4pixels GREATER 0 OR pdfium_partial GREATER 0 OR
       native_partial GREATER 0 OR refmore GREATER 0 OR
       nativemore GREATER 0 OR chroma GREATER 0)
      message(FATAL_ERROR "Only 96DPI can populate edge/quantization diagnostics")
    endif()
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
string(JSON reported_fit GET "${report}" dpi96_analytic_fit_area_abs_rgb)
string(JSON reported_nominal GET "${report}" dpi96_analytic_nominal_area_abs_rgb)
string(JSON reported_far GET "${report}" dpi96_off_ideal_edge_differences)
if(NOT fit96 EQUAL reported_fit OR
   NOT nominal96 EQUAL reported_nominal OR
   NOT far96 EQUAL reported_far)
  message(FATAL_ERROR "96DPI analytic A/B ledger differs from 4 frame rows")
endif()
foreach(field IN ITEMS
  "dpi96_q4_analytic_native_abs_rgb"
  "dpi96_q4_analytic_pdfium_abs_rgb"
  "dpi96_pdfium_nearest_q4_green_distance"
  "dpi96_q4_analytic_native_diff_pixels"
  "dpi96_pdfium_partial_green_pixels"
  "dpi96_native_partial_green_pixels"
  "dpi96_pdfium_more_red_pixels"
  "dpi96_native_more_red_pixels"
  "dpi96_pdfium_chroma_mismatch_pixels")
  string(JSON stored GET "${report}" ${field})
  if(field STREQUAL "dpi96_q4_analytic_native_abs_rgb")
    set(actual "${q4native96}")
  elseif(field STREQUAL "dpi96_q4_analytic_pdfium_abs_rgb")
    set(actual "${q4pdfium96}")
  elseif(field STREQUAL "dpi96_pdfium_nearest_q4_green_distance")
    set(actual "${q4quantum96}")
  elseif(field STREQUAL "dpi96_q4_analytic_native_diff_pixels")
    set(actual "${q4pixels96}")
  elseif(field STREQUAL "dpi96_pdfium_partial_green_pixels")
    set(actual "${partialref96}")
  elseif(field STREQUAL "dpi96_native_partial_green_pixels")
    set(actual "${partialnative96}")
  elseif(field STREQUAL "dpi96_pdfium_more_red_pixels")
    set(actual "${refmore96}")
  elseif(field STREQUAL "dpi96_native_more_red_pixels")
    set(actual "${nativemore96}")
  else()
    set(actual "${chroma96}")
  endif()
  if(NOT stored EQUAL actual)
    message(FATAL_ERROR "96DPI quantum ledger aggregate mismatch: ${field}")
  endif()
endforeach()
string(JSON topedges LENGTH "${report}" dpi96_edge_diff_pixels)
if(NOT topedges EQUAL 5)
  message(FATAL_ERROR "96DPI aggregate edge histogram must have 5 bins")
endif()
set(all_edges 0)
foreach(edge RANGE 0 4)
  string(JSON stored GET "${report}" dpi96_edge_diff_pixels ${edge})
  if(NOT stored EQUAL edgesum_${edge})
    message(FATAL_ERROR "96DPI aggregate edge bins disagree with frame rows")
  endif()
  math(EXPR all_edges "${all_edges}+${stored}")
endforeach()
math(EXPR classified "${all_edges}+${far96}")
if(NOT classified EQUAL changed96)
  message(FATAL_ERROR "96DPI every changed pixel must be classified")
endif()
foreach(kind IN ITEMS "dpi96_integer_snap_abs_rgb" "dpi96_integer_snap_differing_pixels")
  string(JSON count LENGTH "${report}" ${kind})
  if(NOT count EQUAL 5)
    message(FATAL_ERROR "Expected exactly five finite integer snap policies")
  endif()
  foreach(edge RANGE 0 4)
    string(JSON observed GET "${report}" ${kind} ${edge})
    if(kind STREQUAL "dpi96_integer_snap_abs_rgb")
      set(want "${snap_abs_${edge}}")
    else()
      set(want "${snap_different_${edge}}")
    endif()
    if(NOT observed EQUAL want)
      message(FATAL_ERROR "Snap policy total does not match source frame ledger")
    endif()
  endforeach()
endforeach()
message(STATUS
  "12 test-generated ORIGINAL unit7 frames; 72DPI 4/4 exact RGB; "
  "96DPI raster absRGB=${sum96}, fitted analytic=${fit96}, "
  "nominal analytic=${nominal96}, offEdge=${far96}; "
  "144DPI raster absRGB=${sum144}; "
  "96DPI Q4 native absRGB=${q4native96}, PDFium absRGB=${q4pdfium96}, "
  "PDFium off-Q4 green distance=${q4quantum96}, "
  "edge bins=${edgesum_0}/${edgesum_1}/${edgesum_2}/${edgesum_3}/${edgesum_4}; "
  "96DPI finite integer-snap absRGB nearest/floor/ceil/out/in="
  "${snap_abs_0}/${snap_abs_1}/${snap_abs_2}/${snap_abs_3}/${snap_abs_4}; "
  "all high-DPI results DIAGNOSTIC")
