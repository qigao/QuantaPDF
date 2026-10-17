cmake_minimum_required(VERSION 3.24)
# #269: test-only signed WHITE-INK proxy of REAL native C11 output, not
# PDFium internal coverage/edge policy. Source bytes and status stay fixed.
foreach(key IN ITEMS ORACLE BLUE_SOURCE RED_SOURCE BLUE_REPORT RED_REPORT)
  if(NOT DEFINED ${key} OR "${${key}}" STREQUAL "")
    message(FATAL_ERROR "Missing external W Clip signed input ${key}")
  endif()
endforeach()
set(sha_blue "6674dbd092ef8ae55b6ab1cda6a8b966019fbf22f7556291253853303072288b")
set(sha_red "6369893b1f5bd57790f098242d49775094d6c25a1a9b2316aaf1fd1f9a39bc71")
foreach(color IN ITEMS blue red)
  if(color STREQUAL "blue")
    set(source "${BLUE_SOURCE}")
    set(output "${BLUE_REPORT}")
    set(sha_expected "${sha_blue}")
    set(size_expected 514)
    set(expected_document "pdfium.clip-path-v1")
    set(cmd "--external-clip-stroke-pdf")
    set(wanted_w 100)
    set(wanted_h 50)
  else()
    set(source "${RED_SOURCE}")
    set(output "${RED_REPORT}")
    set(sha_expected "${sha_red}")
    set(size_expected 3309)
    set(expected_document "yworks.hidden-clippath-v1")
    set(cmd "--external-hidden-w-candidate-pdf")
    set(wanted_w 500)
    set(wanted_h 500)
  endif()
  if(source STREQUAL output OR source STREQUAL BLUE_REPORT OR
     source STREQUAL RED_REPORT OR BLUE_REPORT STREQUAL RED_REPORT)
    message(FATAL_ERROR "Original source/output alias rejected")
  endif()
  file(SIZE "${source}" source_bytes)
  file(SHA256 "${source}" hash_before)
  if(NOT source_bytes EQUAL size_expected OR
     NOT hash_before STREQUAL sha_expected)
    message(FATAL_ERROR "Original external ${color} PDF provenance changed")
  endif()
  file(REMOVE "${output}" "${output}.tmp")
  execute_process(COMMAND "${ORACLE}" "${cmd}" "${source}"
      --external-report "${output}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr
    TIMEOUT 105)
  if(NOT rc STREQUAL "0" OR NOT EXISTS "${output}")
    message(FATAL_ERROR "Signed ${color} W Clip real-source oracle failed: ${rc}\n${stdout}\n${stderr}")
  endif()
  file(SHA256 "${source}" hash_after)
  if(NOT hash_before STREQUAL hash_after)
    message(FATAL_ERROR "Original external ${color} PDF was modified")
  endif()
  file(READ "${output}" report)
  string(JSON id GET "${report}" document_id)
  string(JSON width GET "${report}" page_width)
  string(JSON height GET "${report}" page_height)
  string(JSON generated GET "${report}" generated_fixture_count)
  string(JSON gross_baseline GET "${report}" gross_error_pixels)
  string(JSON ledger GET "${report}" signed_ink_proxy)
  string(JSON metric GET "${ledger}" metric)
  string(JSON ink_color GET "${ledger}" source_color)
  foreach(key IN ITEMS
      compared_pixels different_pixels exact_pixels
      under_pixels over_pixels color_only_pixels gross_pixels
      under_rgb_abs over_rgb_abs color_only_rgb_abs rgb_abs
      original_ink_sum native_ink_sum)
    string(JSON value GET "${ledger}" ${key})
    set(ledger_${key} "${value}")
    if(value LESS 0)
      message(FATAL_ERROR "Negative signed W Clip ledger field ${key}")
    endif()
  endforeach()
  math(EXPR pixels "${width}*${height}")
  math(EXPR counted "${ledger_exact_pixels}+${ledger_different_pixels}")
  math(EXPR categorized "${ledger_under_pixels}+${ledger_over_pixels}+${ledger_color_only_pixels}")
  math(EXPR rgb_conserved "${ledger_under_rgb_abs}+${ledger_over_rgb_abs}+${ledger_color_only_rgb_abs}")
  math(EXPR ink_cap "255*${pixels}")
  if(NOT id STREQUAL expected_document OR NOT width EQUAL wanted_w OR
     NOT height EQUAL wanted_h OR NOT generated EQUAL 0 OR
     NOT metric STREQUAL "opaque_source_ink_proxy_not_clip_internal_coverage" OR
     NOT ink_color STREQUAL color OR
     NOT ledger_compared_pixels EQUAL pixels OR NOT counted EQUAL pixels OR
     NOT categorized EQUAL ledger_different_pixels OR
     NOT rgb_conserved EQUAL ledger_rgb_abs OR
     NOT ledger_gross_pixels EQUAL gross_baseline OR
     ledger_original_ink_sum GREATER ink_cap OR
     ledger_native_ink_sum GREATER ink_cap)
    message(FATAL_ERROR "External ${color} signed Native/PDFium full RGB conservation failed")
  endif()
  if(color STREQUAL "blue")
    string(JSON outcome GET "${report}" native_observed_outcome)
    string(JSON w GET "${report}" native_w_clip_supported_count)
    string(JSON wstar GET "${report}" native_wstar_clip_supported_count)
    if(NOT outcome STREQUAL "supported-clipped-stroke" OR
       NOT w EQUAL 1 OR NOT wstar EQUAL 0)
      message(FATAL_ERROR "Blue external stroke/Clip admission changed")
    endif()
  else()
    string(JSON state GET "${report}" result)
    string(JSON admitted GET "${report}" official_corpus_admitted)
    string(JSON native GET "${report}" detached_c11_native_raster)
    string(JSON w GET "${report}" native_w_nonzero_clip_path_leaves)
    string(JSON wstar GET "${report}" native_wstar_evenodd_clip_path_leaves)
    string(JSON dag GET "${report}" native_clip_intersection_nodes)
    if(NOT state STREQUAL "candidate_pixel_parity_passed_not_admitted" OR
       admitted OR NOT native STREQUAL "passed" OR NOT w EQUAL 2 OR
       NOT wstar EQUAL 0 OR NOT dag EQUAL 1)
      message(FATAL_ERROR "Red Fill nested W candidate scope changed")
    endif()
  endif()
  message(STATUS "#269 signed ${color}: RGB under=${ledger_under_rgb_abs}"
     " over=${ledger_over_rgb_abs} residual=${ledger_color_only_rgb_abs}"
     " total=${ledger_rgb_abs} (no new admission)")
endforeach()
message(STATUS "external-WClip-signed: passed; two immutable third-party PDFs,"
  " full actual C11 Native vs original PDFium signed ink-proxy; NO backend switch")
