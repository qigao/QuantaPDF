cmake_minimum_required(VERSION 3.24)
# #269 TEST ONLY: exactly one immutable published original, four separately
# generated controls, same closed-triangle blue Stroke. NoClip/fullpage W
# are STRICT UNSUPPORTED+zero-output Native negatives (unimplemented miter).
# Native diff is computed ONLY between two admitted restrictive W Clips.
foreach(arg IN ITEMS ORACLE SOURCE REPORT)
  if(NOT DEFINED ${arg} OR "${${arg}}" STREQUAL "")
    message(FATAL_ERROR "Missing stroke/Clip controlled source/report ${arg}")
  endif()
endforeach()
if(SOURCE STREQUAL REPORT)
  message(FATAL_ERROR "PDF source/report alias rejected")
endif()
file(SIZE "${SOURCE}" source_bytes)
file(SHA256 "${SOURCE}" source_sha)
set(immutable_sha "6674dbd092ef8ae55b6ab1cda6a8b966019fbf22f7556291253853303072288b")
if(NOT source_bytes EQUAL 514 OR NOT source_sha STREQUAL immutable_sha)
  message(FATAL_ERROR "Published original clip_path.pdf source changed")
endif()
file(REMOVE "${REPORT}" "${REPORT}.tmp")
execute_process(COMMAND "${ORACLE}" --269-stroke-clip-ab "${SOURCE}"
        --causal-report "${REPORT}"
   RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr
   TIMEOUT 90)
if(NOT rc STREQUAL "0" OR NOT EXISTS "${REPORT}")
  message(FATAL_ERROR "Controlled Native W Clip/miter rejection failed rc=${rc}\n${stdout}\n${stderr}")
endif()
file(SHA256 "${SOURCE}" after)
if(NOT after STREQUAL source_sha)
  message(FATAL_ERROR "Published PDF overwritten by controlled test")
endif()
file(READ "${REPORT}" report)
string(JSON schema GET "${report}" schema_version)
string(JSON source_type GET "${report}" source_kind)
string(JSON document GET "${report}" published_document_id)
string(JSON original_bytes GET "${report}" published_source_bytes)
string(JSON positive GET "${report}" independent_external_positive_increment)
string(JSON variants LENGTH "${report}" variants)
if(NOT schema EQUAL 2 OR
   NOT source_type STREQUAL "one_unchanged_external_plus_four_test_generated" OR
   NOT document STREQUAL "pdfium.clip-path-v1" OR
   NOT original_bytes EQUAL 514 OR NOT positive EQUAL 0 OR
   NOT variants EQUAL 5)
  message(FATAL_ERROR "Controlled Stroke/W provenance or negative scope invalid")
endif()
foreach(k IN ITEMS
  published_original_provenance_kept
  actual_source_to_owned_ir_to_c11
  fullpage_and_no_clip_reference_equal
  fullpage_and_no_clip_native_fails_closed
  published_and_generated_same_clip_reference_equal
  published_and_generated_same_clip_native_equal
  admitted_narrower_W_uses_same_Stroke
  causal_comparison_uses_only_admitted_native_clips)
  string(JSON yes GET "${report}" ${k})
  if(NOT yes)
    message(FATAL_ERROR "Controlled C11/PDFium admission/identity flag ${k} false")
  endif()
endforeach()
foreach(k IN ITEMS
  pdfium_fallback_used native_raster_mutated public_sdk_backend_switched)
  string(JSON no GET "${report}" ${k})
  if(no)
    message(FATAL_ERROR "Controlled C11/PDFium production/fallback flag ${k} true")
  endif()
endforeach()
set(names independent_original_clipped
   generated_unclipped_same_stroke_UNSUPPORTED
   generated_fullpage_W_same_stroke_UNSUPPORTED
   generated_original_W_same_stroke
   generated_narrower_admitted_W_same_stroke)
set(seen "")
foreach(i RANGE 0 4)
  string(JSON v GET "${report}" variants ${i})
  string(JSON idx GET "${v}" index)
  string(JSON name GET "${v}" name)
  string(JSON kind GET "${v}" source_kind)
  string(JSON hash GET "${v}" source_fnv64)
  string(JSON pages GET "${v}" source_pages)
  string(JSON clips GET "${v}" source_clip_count)
  string(JSON ref_repeat GET "${v}" original_reference_repeat_identical)
  string(JSON outcome GET "${v}" native_outcome)
  string(JSON nat_repeat GET "${v}" native_rgba_repeat_identical)
  string(JSON negative_clean GET "${v}" native_rejection_zero_output)
  string(JSON signed_type TYPE "${v}" signed_ink_proxy)
  list(GET names ${i} expected_name)
  if(i EQUAL 0)
    set(expected_kind "independently_published_original")
  else()
    set(expected_kind "test_generated_control")
  endif()
  if(i EQUAL 1)
    set(expected_clip 0)
  else()
    set(expected_clip 1)
  endif()
  list(FIND seen "${hash}" dup)
  if(NOT dup EQUAL -1)
    message(FATAL_ERROR "Repeated source bytes in causal experiment, variant ${i}")
  endif()
  list(APPEND seen "${hash}")
  if(NOT idx EQUAL i OR NOT name STREQUAL expected_name OR
     NOT kind STREQUAL expected_kind OR
     NOT hash MATCHES "^[0-9a-f]+$" OR
     NOT pages EQUAL 1 OR NOT clips EQUAL expected_clip OR
     NOT ref_repeat)
    message(FATAL_ERROR "Wrong PDF source/control identity at variant ${i}")
  endif()
  if(i EQUAL 1 OR i EQUAL 2)
    if(NOT outcome STREQUAL "unsupported_no_partial_output" OR
       nat_repeat OR NOT negative_clean OR NOT signed_type STREQUAL "NULL")
      message(FATAL_ERROR "Unclipped/full-page W miter must reject with zero Native pixels")
    endif()
  else()
    if(NOT outcome STREQUAL "supported" OR
       NOT nat_repeat OR negative_clean OR
       NOT signed_type STREQUAL "OBJECT")
      message(FATAL_ERROR "Accepted restrictive W must produce repeat-identical Native")
    endif()
    string(JSON signed GET "${v}" signed_ink_proxy)
    string(JSON method GET "${signed}" metric)
    string(JSON color GET "${signed}" source_color)
    if(NOT method STREQUAL "opaque_source_ink_proxy_not_clip_internal_coverage"
       OR NOT color STREQUAL "blue")
      message(FATAL_ERROR "Wrong ink proxy, do not treat as Clip internals")
    endif()
    foreach(k IN ITEMS
      compared_pixels different_pixels exact_pixels under_pixels
      over_pixels color_only_pixels gross_pixels
      under_rgb_abs over_rgb_abs color_only_rgb_abs
      rgb_abs original_ink_sum native_ink_sum)
      string(JSON value GET "${signed}" ${k})
      if(value LESS 0)
        message(FATAL_ERROR "Negative signed RGB field ${k}")
      endif()
      set(s_${i}_${k} "${value}")
    endforeach()
    math(EXPR npixel "${s_${i}_exact_pixels}+${s_${i}_different_pixels}")
    math(EXPR nbucket "${s_${i}_under_pixels}+${s_${i}_over_pixels}+${s_${i}_color_only_pixels}")
    math(EXPR nabs "${s_${i}_under_rgb_abs}+${s_${i}_over_rgb_abs}+${s_${i}_color_only_rgb_abs}")
    if(NOT s_${i}_compared_pixels EQUAL 5000 OR
       NOT npixel EQUAL 5000 OR
       NOT nbucket EQUAL s_${i}_different_pixels OR
       NOT nabs EQUAL s_${i}_rgb_abs OR
       s_${i}_original_ink_sum GREATER 1275000 OR
       s_${i}_native_ink_sum GREATER 1275000)
      message(FATAL_ERROR "Original Native signed C11-vs-PDFium RGB not conserved")
    endif()
  endif()
endforeach()
list(LENGTH seen unique_sources)
if(NOT unique_sources EQUAL 5)
  message(FATAL_ERROR "Missing one published and four distinct generated sources")
endif()
if(NOT s_0_different_pixels EQUAL 100 OR
   NOT s_0_under_pixels EQUAL 100 OR
   NOT s_0_over_pixels EQUAL 0 OR
   NOT s_0_color_only_pixels EQUAL 0 OR
   NOT s_0_rgb_abs EQUAL 2480 OR NOT s_0_gross_pixels EQUAL 0)
  message(FATAL_ERROR "Published W Clip + Stroke prior exact-head ledger regressed")
endif()
foreach(k IN ITEMS
  different_pixels exact_pixels under_pixels over_pixels color_only_pixels
  gross_pixels under_rgb_abs over_rgb_abs color_only_rgb_abs rgb_abs
  original_ink_sum native_ink_sum)
  if(NOT s_0_${k} EQUAL s_3_${k})
    message(FATAL_ERROR "Generated original W Clip no longer matches publisher: ${k}")
  endif()
endforeach()
string(JSON effect GET "${report}" admitted_clip_transfer)
foreach(k IN ITEMS
   reference_changed_pixels native_changed_pixels both_changed_pixels
   membership_change_disagreement_pixels original_clip_error_pixels
   original_error_also_narrow_clip original_error_without_narrow_clip
   narrow_clip_error_absent_from_original
   clip_difference_of_differences_rgb_abs)
  string(JSON v GET "${effect}" ${k})
  if(v LESS 0)
    message(FATAL_ERROR "Negative admitted W Clip difference metric ${k}")
  endif()
  set(t_${k} "${v}")
endforeach()
math(EXPR original_classified
   "${t_original_error_also_narrow_clip}+${t_original_error_without_narrow_clip}")
if(NOT t_original_clip_error_pixels EQUAL 100 OR
   NOT original_classified EQUAL 100 OR
   NOT t_reference_changed_pixels GREATER 0 OR
   NOT t_native_changed_pixels GREATER 0 OR
   t_both_changed_pixels GREATER t_reference_changed_pixels OR
   t_both_changed_pixels GREATER t_native_changed_pixels OR
   t_original_error_also_narrow_clip GREATER s_4_different_pixels OR
   t_narrow_clip_error_absent_from_original GREATER s_4_different_pixels)
  message(FATAL_ERROR "Two-admitted-W-Clip causal attribution not conserved")
endif()
message(STATUS "#269 controlled real C11 W Clip: noClip and fullpage"
  " strictly UNSUPPORTED (zero output); two admitted restrictive W Clips"
  " compare actual Native. Original error=100, narrower error=${s_4_different_pixels};"
  " shared-error=${t_original_error_also_narrow_clip},"
  " original-only=${t_original_error_without_narrow_clip},"
  " differentialRGB=${t_clip_difference_of_differences_rgb_abs};"
  " no production or external-positive promotion.")
