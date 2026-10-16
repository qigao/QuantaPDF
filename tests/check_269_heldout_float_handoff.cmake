# #269: test-only float handoff/Q4 boundary identity gate. Included after
# original-PDF, threshold and signed 144-DPI audits. "report" is immutable.
# The exact-source model is rational integer geometry and never reads PDFium
# to choose pixels. Post-f32 projection must match real native RGBA bytewise.
if(NOT DEFINED report OR report STREQUAL "")
  message(FATAL_ERROR "Prior original-PDF/PDFium report not available")
endif()
string(JSON flag GET "${report}" float_handoff_test_only)
string(JSON n GET "${report}" float_handoff_frames)
string(JSON changed GET "${report}" float_handoff_exact_post_changed)
string(JSON total_exact GET "${report}" float_handoff_exact_rgb_abs)
string(JSON total_pre GET "${report}" float_handoff_pre_rgb_abs)
string(JSON total_post GET "${report}" float_handoff_post_rgb_abs)
if(NOT flag OR NOT n EQUAL 96 OR NOT changed EQUAL 4)
  message(FATAL_ERROR "Float narrowing original-Q4 audit incomplete")
endif()
set(sum_exact 0)
set(sum_pre 0)
set(sum_post 0)
set(found_exact_post 0)
set(found_pre_post 0)
foreach(i RANGE 0 95)
  string(JSON idx GET "${report}" frames ${i} frame)
  string(JSON dpi GET "${report}" frames ${i} dpi)
  string(JSON native GET "${report}" frames ${i} scores 0 rgb_abs)
  string(JSON exact GET "${report}" frames ${i} float_handoff exact_rgb_abs)
  string(JSON pre GET "${report}" frames ${i} float_handoff pre_rgb_abs)
  string(JSON post GET "${report}" frames ${i} float_handoff post_rgb_abs)
  string(JSON mismatch GET "${report}" frames ${i} float_handoff native_rgba_mismatches)
  string(JSON exact_flips GET "${report}" frames ${i} float_handoff exact_post_mask_flips)
  string(JSON narrow_flips GET "${report}" frames ${i} float_handoff pre_post_mask_flips)
  string(JSON only_fill GET "${report}" frames ${i} float_handoff pre_post_fill_only)
  string(JSON only_clip GET "${report}" frames ${i} float_handoff pre_post_clip_only)
  string(JSON both GET "${report}" frames ${i} float_handoff pre_post_both)
  string(JSON changed_exact_pixels GET "${report}" frames ${i} float_handoff exact_post_alpha_pixels)
  string(JSON changed_narrow_pixels GET "${report}" frames ${i} float_handoff pre_post_alpha_pixels)
  if(NOT idx EQUAL i OR NOT mismatch EQUAL 0 OR NOT post EQUAL native)
    message(FATAL_ERROR "Frame ${i}: original Native RGBA and float Q4 model disagree")
  endif()
  math(EXPR component_flips "${only_fill}+${only_clip}+${both}")
  if(component_flips LESS narrow_flips)
    # Sampling stage changes may affect only one component while Clip masks it,
    # hence component membership flips cannot be fewer than effective mask flips.
    message(FATAL_ERROR "Frame ${i}: malformed Q4 Fill/Clip flip classes")
  endif()
  foreach(which IN ITEMS fill clip)
    string(JSON bits LENGTH "${report}" frames ${i} float_handoff ${which}_coeff_bits)
    string(JSON before LENGTH "${report}" frames ${i} float_handoff ${which}_pre_vertices)
    string(JSON after LENGTH "${report}" frames ${i} float_handoff ${which}_post_vertices)
    if(NOT bits EQUAL 6 OR NOT before EQUAL 4 OR NOT after EQUAL 4)
      message(FATAL_ERROR "Frame ${i}: float handoff coefficients/vertices missing")
    endif()
  endforeach()
  math(EXPR sum_exact "${sum_exact}+${exact}")
  math(EXPR sum_pre "${sum_pre}+${pre}")
  math(EXPR sum_post "${sum_post}+${post}")
  math(EXPR delta_exact "${exact}-${post}")
  math(EXPR delta_pre "${pre}-${post}")
  if(NOT delta_exact EQUAL 0)
    math(EXPR found_exact_post "${found_exact_post}+1")
  endif()
  if(NOT delta_pre EQUAL 0)
    math(EXPR found_pre_post "${found_pre_post}+1")
  endif()
  if(i EQUAL 55 OR i EQUAL 58 OR i EQUAL 79 OR i EQUAL 82)
    if(NOT dpi EQUAL 96 OR NOT delta_exact EQUAL 5408 OR
       NOT delta_pre EQUAL 5408 OR NOT only_fill GREATER 0 OR
       NOT only_clip EQUAL 0 OR NOT changed_exact_pixels GREATER 0 OR
       NOT exact_flips GREATER 0 OR NOT narrow_flips GREATER 0)
      message(FATAL_ERROR "Pinned 96DPI Fill Q4 half-pixel counterexample changed: ${i}")
    endif()
  elseif(i EQUAL 28 OR i EQUAL 31)
    if(NOT dpi EQUAL 96 OR NOT delta_exact EQUAL 0 OR
       NOT delta_pre EQUAL 6520 OR NOT only_clip GREATER 0 OR
       NOT only_fill EQUAL 0 OR NOT changed_narrow_pixels GREATER 0 OR
       NOT narrow_flips GREATER 0)
      message(FATAL_ERROR "Pinned 96DPI Clip float-narrowing counterexample changed: ${i}")
    endif()
  elseif(NOT delta_exact EQUAL 0 OR NOT delta_pre EQUAL 0)
    message(FATAL_ERROR "Unexpected float Q4 stage regression at frame ${i}")
  endif()
  if(i EQUAL 68 OR i EQUAL 71)
    if(NOT dpi EQUAL 144 OR NOT exact EQUAL 53506 OR
       NOT pre EQUAL 53506 OR NOT post EQUAL 53506)
      message(FATAL_ERROR "144DPI historical heldout Q4 regression changed")
    endif()
  endif()
endforeach()
if(NOT found_exact_post EQUAL 4 OR NOT found_pre_post EQUAL 6 OR
   NOT sum_exact EQUAL total_exact OR NOT sum_pre EQUAL total_pre OR
   NOT sum_post EQUAL total_post OR
   NOT sum_exact EQUAL 3839988 OR NOT sum_pre EQUAL 3853028 OR
   NOT sum_post EQUAL 3818356)
  message(FATAL_ERROR "Exact/pre-float/post-float RGB conservation failed")
endif()
message(STATUS "#269 float Q4 handoff audit: all 96 native RGBA predictions exact;"
  " isolated Fill 4x5408 and Clip 2x6520 at 96DPI; 144DPI #68/#71 preserved."
  " Test-only; no Q4 pixel/threshold/production changes.")
