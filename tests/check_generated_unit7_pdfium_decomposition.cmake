cmake_minimum_required(VERSION 3.20)

# Three independent test-generated ORIGINAL PDF source files at the same
# CropBox/Rotate/UserUnit: Fill+W Clip, Fill-only, and Clip-only.
# Validate truthful numeric source-PDFium observation, not pixel compatibility.
if(NOT DEFINED ORACLE_EXECUTABLE OR ORACLE_EXECUTABLE STREQUAL "" OR
   NOT DEFINED REPORT_FILE OR REPORT_FILE STREQUAL "")
  message(FATAL_ERROR "Explicit Source-PDFium primitive decomposition required")
endif()
file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(
  COMMAND "${ORACLE_EXECUTABLE}"
    --generated-unit7-pdfium-mask-decomposition
    --decomposition-report "${REPORT_FILE}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE stdout_text
  ERROR_VARIABLE stderr_text TIMEOUT 45)
if(NOT rc STREQUAL "0")
  message(FATAL_ERROR
    "Original PDFium mask decomposition failed:\n"
    "stdout=${stdout_text}\nstderr=${stderr_text}\nrc=${rc}")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "Original source PDFium decomposition output not published")
endif()
file(READ "${REPORT_FILE}" report)
foreach(field IN ITEMS
  "\"source_kind\":\"test_generated_original_pdf\""
  "\"original_producer\":\"test_owned_qpdf_writer\""
  "\"source_files\":48"
  "\"source_byte_repeat_count\":48"
  "\"mask_pixel_count\":338400"
  "\"frame_count\":16"
  "\"dpi\":96"
  "\"page_user_unit\":7"
  "\"all_measurements_from_original_pdfium\":true"
  "\"native_raster\":\"not_run\""
  "\"external_original_provenance_verified\":false"
  "\"external_native_positive_increment\":0"
  "\"public_sdk_backend_switched\":false")
  string(FIND "${report}" "${field}" marker)
  if(marker EQUAL -1)
    message(FATAL_ERROR "Missing source-only decomposition/nonpromotion marker: ${field}")
  endif()
endforeach()
foreach(source IN ITEMS combined fill_only clip_only)
  string(JSON count GET "${report}" source_kind_counts ${source})
  if(NOT count EQUAL 16)
    message(FATAL_ERROR "Each content variant must have 16 original source PDFs")
  endif()
endforeach()
string(JSON rows LENGTH "${report}" frames)
if(NOT rows EQUAL 16)
  message(FATAL_ERROR "Original 4×4 source matrix requires 16 independent mode rows")
endif()

set(sum_different 0)
set(sum_missing 0)
set(sum_extra 0)
set(sum_combined 0)
set(sum_binary 0)
set(sum_exact 0)
foreach(i RANGE 0 15)
  string(JSON variant GET "${report}" frames ${i} crop_variant)
  string(JSON label GET "${report}" frames ${i} crop_label)
  string(JSON rotation GET "${report}" frames ${i} rotation)
  string(JSON width GET "${report}" frames ${i} width)
  string(JSON height GET "${report}" frames ${i} height)
  string(JSON red_combined GET "${report}" frames ${i} combined_red)
  string(JSON red_fill GET "${report}" frames ${i} fill_only_red)
  string(JSON red_clip GET "${report}" frames ${i} clip_only_red)
  string(JSON and_pixels GET "${report}" frames ${i} predicted_binary_and)
  string(JSON missing GET "${report}" frames ${i} missing_from_combined)
  string(JSON extra GET "${report}" frames ${i} extra_in_combined)
  string(JSON different GET "${report}" frames ${i} combined_vs_and_different)
  string(JSON binary GET "${report}" frames ${i} all_binary)
  string(JSON exact GET "${report}" frames ${i} exact_binary_intersection)
  string(JSON nonbinary_length LENGTH "${report}" frames ${i} nonbinary_counts)
  string(JSON rectangular_length LENGTH "${report}" frames ${i} rectangular_flags)
  math(EXPR expected_variant "${i}/4")
  math(EXPR expected_rotation "(${i}%4)*90")
  if(NOT variant EQUAL expected_variant OR
     NOT rotation EQUAL expected_rotation OR
     NOT nonbinary_length EQUAL 3 OR NOT rectangular_length EQUAL 3)
    message(FATAL_ERROR "Unrecognized generated original mode or component order")
  endif()
  if(variant EQUAL 0)
    set(expected_label "baseline")
  elseif(variant EQUAL 1)
    set(expected_label "x_plus_0_125")
  elseif(variant EQUAL 2)
    set(expected_label "y_plus_0_125")
  else()
    set(expected_label "xy_plus_0_375")
  endif()
  if(NOT label STREQUAL expected_label)
    message(FATAL_ERROR "Unexpected CropBox perturbation label")
  endif()
  if(rotation EQUAL 0 OR rotation EQUAL 180)
    set(expected_width 94)
    set(expected_height 75)
  else()
    set(expected_width 75)
    set(expected_height 94)
  endif()
  if(NOT width EQUAL expected_width OR NOT height EQUAL expected_height)
    message(FATAL_ERROR "Source-PDFium dimensions not supported by bounded device grid")
  endif()
  math(EXPR pixels "${width}* ${height}")
  foreach(value IN ITEMS red_combined red_fill red_clip)
    if(${value} LESS 1 OR ${value} GREATER pixels)
      message(FATAL_ERROR "Unexpected absent or oversized original PDFium source mask")
    endif()
  endforeach()
  if(and_pixels GREATER pixels OR missing GREATER pixels OR
     extra GREATER pixels OR different GREATER pixels OR
     (NOT binary EQUAL 0 AND NOT binary EQUAL 1) OR
     (NOT exact EQUAL 0 AND NOT exact EQUAL 1))
    message(FATAL_ERROR "Malformed bounded source-PDFium mask comparison values")
  endif()
  math(EXPR sum_parts "${missing}+${extra}")
  math(EXPR predicted_count "${red_combined}+${missing}-${extra}")
  if(NOT different EQUAL sum_parts OR NOT and_pixels EQUAL predicted_count)
    message(FATAL_ERROR "Observed combined-vs-AND mismatch does not reconcile")
  endif()
  set(has_nonbinary 0)
  foreach(kind RANGE 0 2)
    string(JSON nonbinary GET "${report}" frames ${i} nonbinary_counts ${kind})
    string(JSON rectangular GET "${report}" frames ${i} rectangular_flags ${kind})
    if(nonbinary GREATER pixels OR
       (NOT rectangular EQUAL 0 AND NOT rectangular EQUAL 1))
      message(FATAL_ERROR "Malformed source-PDFium mask/nonbinary flag")
    endif()
    if(nonbinary GREATER 0)
      set(has_nonbinary 1)
    endif()
    if(kind EQUAL 0)
      set(source_name "combined")
    elseif(kind EQUAL 1)
      set(source_name "fill_only")
    else()
      set(source_name "clip_only")
    endif()
    string(JSON bbox_count LENGTH "${report}" frames ${i} ${source_name}_bbox)
    if(NOT bbox_count EQUAL 4)
      message(FATAL_ERROR "Expected 4 exclusive bbox edges for each PDFium source")
    endif()
    foreach(edge RANGE 0 3)
      string(JSON bound GET "${report}" frames ${i} ${source_name}_bbox ${edge})
      if(edge EQUAL 0)
        set(left "${bound}")
      elseif(edge EQUAL 1)
        set(top "${bound}")
      elseif(edge EQUAL 2)
        set(right "${bound}")
      else()
        set(bottom "${bound}")
      endif()
    endforeach()
    if(left LESS 0 OR top LESS 0 OR right GREATER width OR
       bottom GREATER height OR left GREATER_EQUAL right OR
       top GREATER_EQUAL bottom)
      message(FATAL_ERROR "Observed original PDFium mask bbox exceeds grid")
    endif()
    math(EXPR area "(${right}-${left})*(${bottom}-${top})")
    if(kind EQUAL 0)
      set(source_red "${red_combined}")
    elseif(kind EQUAL 1)
      set(source_red "${red_fill}")
    else()
      set(source_red "${red_clip}")
    endif()
    if(source_red GREATER area OR
       (rectangular EQUAL 1 AND (NOT source_red EQUAL area OR
                                 NOT nonbinary EQUAL 0)))
      message(FATAL_ERROR "False binary rectangular mask claim")
    endif()
  endforeach()
  if((binary EQUAL 1 AND has_nonbinary EQUAL 1) OR
     (binary EQUAL 0 AND has_nonbinary EQUAL 0) OR
     (exact EQUAL 1 AND (NOT binary EQUAL 1 OR NOT different EQUAL 0)) OR
     (binary EQUAL 1 AND different EQUAL 0 AND NOT exact EQUAL 1))
    message(FATAL_ERROR "Independent mask compositing evidence flags disagree")
  endif()
  math(EXPR sum_different "${sum_different}+${different}")
  math(EXPR sum_missing "${sum_missing}+${missing}")
  math(EXPR sum_extra "${sum_extra}+${extra}")
  math(EXPR sum_combined "${sum_combined}+${red_combined}")
  math(EXPR sum_binary "${sum_binary}+${binary}")
  math(EXPR sum_exact "${sum_exact}+${exact}")
endforeach()
foreach(metric IN ITEMS
  intersection_differing_pixels
  intersection_missing_red_pixels
  intersection_extra_red_pixels
  combined_red_pixel_count
  binary_modes
  exact_mask_intersection_modes)
  string(JSON reported GET "${report}" ${metric})
  if(metric STREQUAL "intersection_differing_pixels")
    set(expected "${sum_different}")
  elseif(metric STREQUAL "intersection_missing_red_pixels")
    set(expected "${sum_missing}")
  elseif(metric STREQUAL "intersection_extra_red_pixels")
    set(expected "${sum_extra}")
  elseif(metric STREQUAL "combined_red_pixel_count")
    set(expected "${sum_combined}")
  elseif(metric STREQUAL "binary_modes")
    set(expected "${sum_binary}")
  else()
    set(expected "${sum_exact}")
  endif()
  if(NOT reported EQUAL expected)
    message(FATAL_ERROR "Original PDFium decomposition aggregate mismatch: ${metric}")
  endif()
endforeach()
message(STATUS
  "48 original generated PDF sources: 16 pages × 3 component masks, "
  "16 mode rows, PDFium binary modes=${sum_binary}/16, "
  "combined=separate PDFium mask AND for ${sum_exact}/16 modes; "
  "mismatched pixels=${sum_different} (missing=${sum_missing}, "
  "extra=${sum_extra}). No external source native-positive or SDK cutover.")
