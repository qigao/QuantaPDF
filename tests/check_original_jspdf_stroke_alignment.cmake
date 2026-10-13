# Pinned original jsPDF, independent PDFium raster vs private C11 grid.
cmake_minimum_required(VERSION 3.24)
foreach(arg IN ITEMS ORACLE SOURCE REPORT)
  if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${arg}")
  endif()
endforeach()
if(IS_SYMLINK "${SOURCE}")
  message(FATAL_ERROR "Original source must not be a symlink")
endif()
file(SIZE "${SOURCE}" size)
file(SHA256 "${SOURCE}" before)
if(NOT size EQUAL 3344 OR NOT before STREQUAL
    "179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e")
  message(FATAL_ERROR "Original jsPDF identity changed")
endif()
execute_process(COMMAND "${ORACLE}" "${SOURCE}" TIMEOUT 30
  RESULT_VARIABLE rc OUTPUT_VARIABLE json ERROR_VARIABLE stderr)
file(SHA256 "${SOURCE}" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "jsPDF source was mutated")
endif()
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "Stroke pixel phase A/B failed: ${rc}: ${stderr}")
endif()
foreach(field IN ITEMS result document_id original_pdfium_raster_invocations
    native_raster_invocations dpi_count source_coordinate_basis
    source_line_center_y_display_points source_stroke_width_points
    pixel_parity_qualified original_pixel_positive_count
    official_corpus_admitted total_row_observations
    max_pdfium_api_coordinate_error_points
    sample_centers_independently_qualified total_vertical_observations)
  string(JSON got_${field} GET "${json}" ${field})
endforeach()
string(JSON count LENGTH "${json}" cases)
string(JSON xs_length LENGTH "${json}" source_x_points)
string(JSON x0 GET "${json}" source_x_points 0)
string(JSON x1 GET "${json}" source_x_points 1)
if(NOT got_result STREQUAL "original_jspdf_stroke_pixel_phase_diagnostic"
   OR NOT got_document_id STREQUAL "jspdf.w3s-clip-v1"
   OR NOT got_original_pdfium_raster_invocations EQUAL 3
   OR NOT got_native_raster_invocations EQUAL 0
   OR NOT got_dpi_count EQUAL 3
   OR NOT got_source_coordinate_basis STREQUAL "PDF_user_Y_up_to_display_Y_down"
   OR NOT got_source_line_center_y_display_points EQUAL 20
   OR NOT got_source_stroke_width_points EQUAL 1
   OR got_pixel_parity_qualified OR got_original_pixel_positive_count
   OR got_official_corpus_admitted
   OR got_sample_centers_independently_qualified
   OR NOT got_total_row_observations EQUAL 42
   OR NOT got_total_vertical_observations EQUAL 42
   OR NOT count EQUAL 3 OR NOT xs_length EQUAL 2
   OR NOT x0 EQUAL 200 OR NOT x1 EQUAL 220
   OR got_max_pdfium_api_coordinate_error_points GREATER 0.0005
   OR got_max_pdfium_api_coordinate_error_points LESS 0)
  message(FATAL_ERROR "Original phase A/B overstates or omits evidence")
endif()
set(observed_total 0)
set(vertical_total 0)
foreach(i RANGE 0 2)
  if(i EQUAL 0)
    set(expected_dpi 72)
    set(expected_w 596)
    set(expected_h 842)
  elseif(i EQUAL 1)
    set(expected_dpi 96)
    set(expected_w 794)
    set(expected_h 1123)
  else()
    set(expected_dpi 144)
    set(expected_w 1191)
    set(expected_h 1684)
  endif()
  foreach(field IN ITEMS dpi device_width device_height samples
      max_page_api_error_points sample_phase_is_test_only
      pdfium_aa_sample_centers_proven)
    string(JSON case_${field} GET "${json}" cases ${i} ${field})
  endforeach()
  string(JSON phase_n LENGTH "${json}" cases ${i} phase_trial_abs_gray_error)
  string(JSON maxima_n LENGTH "${json}" cases ${i} phase_trial_max_gray_delta)
  string(JSON rows LENGTH "${json}" cases ${i} observations)
  if(NOT case_dpi EQUAL expected_dpi
     OR NOT case_device_width EQUAL expected_w
     OR NOT case_device_height EQUAL expected_h
     OR NOT case_samples EQUAL 14 OR NOT rows EQUAL 14
     OR NOT phase_n EQUAL 3 OR NOT maxima_n EQUAL 3
     OR case_max_page_api_error_points LESS 0
     OR case_max_page_api_error_points GREATER 0.0005
     OR NOT case_sample_phase_is_test_only
     OR case_pdfium_aa_sample_centers_proven)
    message(FATAL_ERROR "DPI-specific source/coordinate evidence incomplete")
  endif()
  foreach(phase RANGE 0 2)
    string(JSON score GET "${json}" cases ${i} phase_trial_abs_gray_error ${phase})
    string(JSON biggest GET "${json}" cases ${i} phase_trial_max_gray_delta ${phase})
    if(score LESS 0 OR score GREATER 3570
       OR biggest LESS 0 OR biggest GREATER 255)
      message(FATAL_ERROR "A/B phase sample score invalid")
    endif()
  endforeach()
  foreach(j RANGE 0 13)
    math(EXPR col_index "${j}/7")
    if(col_index EQUAL 0)
      set(expected_source_x 200)
    else()
      set(expected_source_x 220)
    endif()
    string(JSON actual_source_x GET "${json}" cases ${i} observations ${j} source_x)
    string(JSON dev_x GET "${json}" cases ${i} observations ${j} device_x)
    string(JSON dev_y GET "${json}" cases ${i} observations ${j} device_y)
    string(JSON api_error GET "${json}" cases ${i} observations ${j} device_anchor_error_points)
    string(JSON rgb_size LENGTH "${json}" cases ${i} observations ${j} pdfium_rgb)
    string(JSON alpha_size LENGTH "${json}" cases ${i} observations ${j} phase_trial_alpha)
    string(JSON deltas_size LENGTH "${json}" cases ${i} observations ${j} abs_delta_trial)
    string(JSON unshifted GET "${json}" cases ${i} observations ${j} c11_unshifted_4x4_alpha)
    string(JSON phase0 GET "${json}" cases ${i} observations ${j} phase_trial_alpha 1)
    if(NOT actual_source_x EQUAL expected_source_x
       OR dev_x LESS 0 OR dev_x GREATER_EQUAL expected_w
       OR dev_y LESS 0 OR dev_y GREATER_EQUAL expected_h
       OR api_error LESS 0 OR api_error GREATER 0.0005
       OR NOT rgb_size EQUAL 3 OR NOT alpha_size EQUAL 3
       OR NOT deltas_size EQUAL 3 OR NOT unshifted EQUAL phase0)
      message(FATAL_ERROR "Probe identity/coordinate provenance invalid")
    endif()
    foreach(channel RANGE 0 2)
      string(JSON rgb GET "${json}" cases ${i} observations ${j} pdfium_rgb ${channel})
      string(JSON alpha GET "${json}" cases ${i} observations ${j} phase_trial_alpha ${channel})
      string(JSON diff GET "${json}" cases ${i} observations ${j} abs_delta_trial ${channel})
      if(rgb LESS 0 OR rgb GREATER 255
         OR alpha LESS 0 OR alpha GREATER 255
         OR diff LESS 0 OR diff GREATER 255)
        message(FATAL_ERROR "Invalid original PDFium RGB / bounded C11 phase")
      endif()
    endforeach()
  endforeach()
  math(EXPR observed_total "${observed_total}+${rows}")

  # Independent vertical original S stroke at x=250, y=60/80 (Y-down).
  foreach(vfield IN ITEMS source_x_points samples
      max_page_api_error_points sample_phase_is_test_only
      pdfium_aa_sample_centers_proven)
    string(JSON vertical_${vfield} GET "${json}" cases ${i} vertical_stroke ${vfield})
  endforeach()
  string(JSON vsource_len LENGTH "${json}" cases ${i} vertical_stroke source_y_display_points)
  string(JSON vrows LENGTH "${json}" cases ${i} vertical_stroke observations)
  string(JSON vtrial_count LENGTH "${json}" cases ${i} vertical_stroke phase_trial_abs_gray_error)
  string(JSON vmax_count LENGTH "${json}" cases ${i} vertical_stroke phase_trial_max_gray_delta)
  if(NOT vertical_source_x_points EQUAL 250
     OR NOT vertical_samples EQUAL 14 OR NOT vrows EQUAL 14
     OR NOT vsource_len EQUAL 2 OR NOT vtrial_count EQUAL 3
     OR NOT vmax_count EQUAL 3
     OR vertical_max_page_api_error_points LESS 0
     OR vertical_max_page_api_error_points GREATER 0.0005
     OR NOT vertical_sample_phase_is_test_only
     OR vertical_pdfium_aa_sample_centers_proven)
    message(FATAL_ERROR "Original vertical Stroke source/DPI provenance invalid")
  endif()
  string(JSON vy0 GET "${json}" cases ${i} vertical_stroke source_y_display_points 0)
  string(JSON vy1 GET "${json}" cases ${i} vertical_stroke source_y_display_points 1)
  if(NOT vy0 EQUAL 60 OR NOT vy1 EQUAL 80)
    message(FATAL_ERROR "Original vertical Stroke control was changed")
  endif()
  foreach(k RANGE 0 2)
    string(JSON vscore GET "${json}" cases ${i} vertical_stroke phase_trial_abs_gray_error ${k})
    string(JSON vmax GET "${json}" cases ${i} vertical_stroke phase_trial_max_gray_delta ${k})
    if(vscore LESS 0 OR vscore GREATER 3570 OR
       vmax LESS 0 OR vmax GREATER 255)
      message(FATAL_ERROR "Vertical phase A/B score invalid")
    endif()
  endforeach()
  foreach(j RANGE 0 13)
    math(EXPR ygroup "${j}/7")
    if(ygroup EQUAL 0)
      set(expected_source_y 60)
    else()
      set(expected_source_y 80)
    endif()
    string(JSON source_y GET "${json}" cases ${i} vertical_stroke observations ${j} source_y_display)
    string(JSON dev_x GET "${json}" cases ${i} vertical_stroke observations ${j} device_x)
    string(JSON dev_y GET "${json}" cases ${i} vertical_stroke observations ${j} device_y)
    string(JSON err GET "${json}" cases ${i} vertical_stroke observations ${j} device_anchor_error_points)
    string(JSON rgb_n LENGTH "${json}" cases ${i} vertical_stroke observations ${j} pdfium_rgb)
    string(JSON alphas_n LENGTH "${json}" cases ${i} vertical_stroke observations ${j} phase_trial_alpha)
    string(JSON deltas_n LENGTH "${json}" cases ${i} vertical_stroke observations ${j} abs_delta_trial)
    string(JSON current GET "${json}" cases ${i} vertical_stroke observations ${j} c11_unshifted_4x4_alpha)
    string(JSON neutral GET "${json}" cases ${i} vertical_stroke observations ${j} phase_trial_alpha 1)
    if(NOT source_y EQUAL expected_source_y
       OR dev_x LESS 0 OR dev_x GREATER_EQUAL expected_w
       OR dev_y LESS 0 OR dev_y GREATER_EQUAL expected_h
       OR err LESS 0 OR err GREATER 0.0005
       OR NOT rgb_n EQUAL 3 OR NOT alphas_n EQUAL 3 OR
       NOT deltas_n EQUAL 3 OR NOT current EQUAL neutral)
      message(FATAL_ERROR "Vertical A/B source geometry or alpha gate invalid")
    endif()
    foreach(k RANGE 0 2)
      string(JSON rgb GET "${json}" cases ${i} vertical_stroke observations ${j} pdfium_rgb ${k})
      string(JSON alpha GET "${json}" cases ${i} vertical_stroke observations ${j} phase_trial_alpha ${k})
      string(JSON difference GET "${json}" cases ${i} vertical_stroke observations ${j} abs_delta_trial ${k})
      if(rgb LESS 0 OR rgb GREATER 255 OR
         alpha LESS 0 OR alpha GREATER 255 OR
         difference LESS 0 OR difference GREATER 255)
        message(FATAL_ERROR "Vertical A/B RGB/coverage outside physical bounds")
      endif()
    endforeach()
  endforeach()
  math(EXPR vertical_total "${vertical_total}+${vrows}")

endforeach()
if(NOT vertical_total EQUAL got_total_vertical_observations)
  message(FATAL_ERROR "Incomplete original vertical Stroke scans")
endif()
if(NOT observed_total EQUAL got_total_row_observations)
  message(FATAL_ERROR "Incomplete Stroke row scans")
endif()

# Published as a quarantined test artifact, never an original-PDF positive.
# Reject symlink or source aliases before writing any stage report.
get_filename_component(report_dir "${REPORT}" DIRECTORY)
get_filename_component(report_base "${REPORT}" NAME)
file(REAL_PATH "${SOURCE}" original_real)
file(REAL_PATH "${report_dir}" report_dir_real)
if("${report_dir_real}/${report_base}" STREQUAL "${original_real}"
   OR IS_SYMLINK "${REPORT}" OR IS_SYMLINK "${REPORT}.tmp")
  message(FATAL_ERROR "Stroke phase report aliases original or symlink")
endif()
file(WRITE "${REPORT}.tmp" "${json}\n")
file(RENAME "${REPORT}.tmp" "${REPORT}")

message(STATUS "original-jsPDF-Stroke-pixel-phase: pinned_sha256=${after}\n${json}")
