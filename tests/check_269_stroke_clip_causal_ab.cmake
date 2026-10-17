cmake_minimum_required(VERSION 3.24)
# #269: TEST ONLY. Exactly one unchanged independently published blue
# W Clip original and three separately-generated controlled source PDFs.
# Actual detached IR -> C11 Native bytes versus original source PDFium;
# no Provider, production Raster, public ABI/SDK, or external-positive change.
foreach(arg IN ITEMS ORACLE SOURCE REPORT)
  if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
    message(FATAL_ERROR "Missing stroke/Clip causal A/B input ${arg}")
  endif()
endforeach()
if(SOURCE STREQUAL REPORT)
  message(FATAL_ERROR "Original PDF cannot be output report")
endif()
file(SIZE "${SOURCE}" source_bytes)
file(SHA256 "${SOURCE}" source_sha)
set(immutable_sha "6674dbd092ef8ae55b6ab1cda6a8b966019fbf22f7556291253853303072288b")
if(NOT source_bytes EQUAL 514 OR NOT source_sha STREQUAL immutable_sha)
  message(FATAL_ERROR "Original externally published clip_path.pdf bytes changed")
endif()
file(REMOVE "${REPORT}" "${REPORT}.tmp")
execute_process(
  COMMAND "${ORACLE}" --269-stroke-clip-ab "${SOURCE}"
          --causal-report "${REPORT}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr
  TIMEOUT 60)
if(NOT rc STREQUAL "0" OR NOT EXISTS "${REPORT}")
  message(FATAL_ERROR "Stroke/Clip controlled Native/PDFium oracle failed ${rc}\n${stdout}\n${stderr}")
endif()
file(SHA256 "${SOURCE}" afterward)
if(NOT source_sha STREQUAL afterward)
  message(FATAL_ERROR "Original PDF was overwritten or modified")
endif()
file(READ "${REPORT}" report)
string(JSON version GET "${report}" schema_version)
string(JSON kind GET "${report}" source_kind)
string(JSON doc GET "${report}" published_document_id)
string(JSON source_size GET "${report}" published_source_bytes)
string(JSON inc GET "${report}" independent_external_positive_increment)
string(JSON variant_n LENGTH "${report}" variants)
if(NOT version EQUAL 1 OR
   NOT kind STREQUAL "one_unchanged_external_plus_three_test_generated" OR
   NOT doc STREQUAL "pdfium.clip-path-v1" OR
   NOT source_size EQUAL 514 OR NOT inc EQUAL 0 OR
   NOT variant_n EQUAL 4)
  message(FATAL_ERROR "Real-source Stroke/Clip A/B scope contaminated")
endif()
foreach(k IN ITEMS
   published_original_provenance_kept actual_source_to_owned_ir_to_c11
   fullpage_and_no_clip_reference_equal
   fullpage_and_no_clip_native_equal
   published_and_generated_same_clip_reference_equal
   published_and_generated_same_clip_native_equal)
  string(JSON flag GET "${report}" ${k})
  if(NOT flag)
    message(FATAL_ERROR "Strict original/Native A/B identity gate ${k} absent")
  endif()
endforeach()
foreach(k IN ITEMS pdfium_fallback_used native_raster_mutated
                   public_sdk_backend_switched)
  string(JSON flag GET "${report}" ${k})
  if(flag)
    message(FATAL_ERROR "Stroke/Clip A/B production/fallback gate ${k} violated")
  endif()
endforeach()
set(expected_names
   independent_original_clipped
   generated_unclipped_same_stroke
   generated_fullpage_W_same_stroke
   generated_original_W_same_stroke)
set(seen_hashes "")
foreach(i RANGE 0 3)
  string(JSON variant GET "${report}" variants ${i})
  string(JSON idx GET "${variant}" index)
  string(JSON name GET "${variant}" name)
  string(JSON source_kind GET "${variant}" source_kind)
  string(JSON hash GET "${variant}" source_fnv64)
  string(JSON source_pages GET "${variant}" source_pages)
  string(JSON clips GET "${variant}" source_clip_count)
  string(JSON ref_repeat GET "${variant}" original_reference_repeat_identical)
  string(JSON native_repeat GET "${variant}" native_rgba_repeat_identical)
  list(GET expected_names ${i} expected)
  if(i EQUAL 0)
    set(expected_kind "independently_published_original")
    set(expected_clip 1)
  else()
    set(expected_kind "test_generated_control")
    if(i EQUAL 1)
      set(expected_clip 0)
    else()
      set(expected_clip 1)
    endif()
    list(FIND seen_hashes "${hash}" repeated)
    if(NOT repeated EQUAL -1)
      message(FATAL_ERROR "Two supposedly distinct controlled source PDFs reused bytes")
    endif()
  endif()
  list(APPEND seen_hashes "${hash}")
  if(NOT idx EQUAL i OR NOT name STREQUAL expected OR
     NOT source_kind STREQUAL expected_kind OR
     NOT hash MATCHES "^[0-9a-f]+$" OR
     NOT source_pages EQUAL 1 OR NOT clips EQUAL expected_clip OR
     NOT ref_repeat OR NOT native_repeat)
    message(FATAL_ERROR "Stroke/Clip variant source/ownership mismatch ${i}")
  endif()
  string(JSON signed GET "${variant}" signed_ink_proxy)
  foreach(k IN ITEMS
     compared_pixels different_pixels exact_pixels under_pixels
     over_pixels color_only_pixels gross_pixels
     under_rgb_abs over_rgb_abs color_only_rgb_abs
     rgb_abs original_ink_sum native_ink_sum)
    string(JSON x GET "${signed}" ${k})
    if(x LESS 0)
      message(FATAL_ERROR "Negative original signed stroke pixel quantity ${k}")
    endif()
    set(v_${i}_${k} "${x}")
  endforeach()
  string(JSON method GET "${signed}" metric)
  string(JSON color GET "${signed}" source_color)
  math(EXPR all_pixels "${v_${i}_exact_pixels}+${v_${i}_different_pixels}")
  math(EXPR classify "${v_${i}_under_pixels}+${v_${i}_over_pixels}+${v_${i}_color_only_pixels}")
  math(EXPR errors "${v_${i}_under_rgb_abs}+${v_${i}_over_rgb_abs}+${v_${i}_color_only_rgb_abs}")
  if(NOT method STREQUAL "opaque_source_ink_proxy_not_clip_internal_coverage" OR
     NOT color STREQUAL "blue" OR
     NOT v_${i}_compared_pixels EQUAL 5000 OR
     NOT all_pixels EQUAL 5000 OR
     NOT classify EQUAL v_${i}_different_pixels OR
     NOT errors EQUAL v_${i}_rgb_abs)
    message(FATAL_ERROR "Original Native stroke signed conservation failed variant ${i}")
  endif()
  if(i EQUAL 0)
    if(NOT v_0_different_pixels EQUAL 100 OR
       NOT v_0_under_pixels EQUAL 100 OR
       NOT v_0_over_pixels EQUAL 0 OR
       NOT v_0_color_only_pixels EQUAL 0 OR
       NOT v_0_rgb_abs EQUAL 2480 OR
       NOT v_0_gross_pixels EQUAL 0)
      message(FATAL_ERROR "Published clip_path.pdf Native/PDFium regression changed")
    endif()
  endif()
endforeach()
# Exact semantic generated original-W == publisher original, and full page
# W == no W, both for actual C11 bytes and independently PDFium RGB bytes.
foreach(k IN ITEMS
   different_pixels exact_pixels under_pixels over_pixels color_only_pixels
   gross_pixels under_rgb_abs over_rgb_abs color_only_rgb_abs rgb_abs
   original_ink_sum native_ink_sum)
  if(NOT v_0_${k} EQUAL v_3_${k} OR
     NOT v_1_${k} EQUAL v_2_${k})
    message(FATAL_ERROR "Controlled Stroke/Clip pair differs before attribution: ${k}")
  endif()
endforeach()
string(JSON transfer GET "${report}" clip_transfer)
foreach(k IN ITEMS
  reference_changed_pixels native_changed_pixels both_changed_pixels
  membership_change_disagreement_pixels clipped_error_pixels
  clipped_error_also_unclipped clipped_error_without_unclipped
  unclipped_error_suppressed_by_clip clip_difference_of_differences_rgb_abs)
  string(JSON x GET "${transfer}" ${k})
  if(x LESS 0)
    message(FATAL_ERROR "Negative stroke/Clip difference-of-differences ${k}")
  endif()
  set(t_${k} "${x}")
endforeach()
math(EXPR clip_errors "${t_clipped_error_also_unclipped}+${t_clipped_error_without_unclipped}")
if(NOT clip_errors EQUAL 100 OR
   NOT t_clipped_error_pixels EQUAL 100 OR
   NOT t_reference_changed_pixels GREATER 0 OR
   NOT t_native_changed_pixels GREATER 0 OR
   t_both_changed_pixels GREATER t_reference_changed_pixels OR
   t_both_changed_pixels GREATER t_native_changed_pixels OR
   t_clipped_error_also_unclipped GREATER v_1_different_pixels OR
   t_unclipped_error_suppressed_by_clip GREATER v_1_different_pixels)
  message(FATAL_ERROR "Controlled W Clip causal subtraction/overlap not conserved")
endif()
message(STATUS "#269 causal W Clip A/B: original blue clipped diff=100,"
  " unclipped diff=${v_1_different_pixels},"
  " error-carried-by-Stroke=${t_clipped_error_also_unclipped},"
  " clip-only-different=${t_clipped_error_without_unclipped},"
  " Clip-difference-RGB=${t_clip_difference_of_differences_rgb_abs};"
  " no native Raster or external-positive promotion.")
