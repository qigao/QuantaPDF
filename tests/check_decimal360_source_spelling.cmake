cmake_minimum_required(VERSION 3.20)
# #360: strict source-backed spelling/bit-pattern/device-size observation gate.
if(NOT DEFINED ORACLE_EXECUTABLE OR ORACLE_EXECUTABLE STREQUAL "" OR
   NOT DEFINED REPORT_FILE OR REPORT_FILE STREQUAL "")
  message(FATAL_ERROR "Explicit #360 literal-original oracle/report required")
endif()
file(REMOVE "${REPORT_FILE}" "${REPORT_FILE}.tmp")
execute_process(
  COMMAND "${ORACLE_EXECUTABLE}" --decimal360-source-spellings
    --source-spellings-report "${REPORT_FILE}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 42)
if(NOT rc STREQUAL "0")
  message(FATAL_ERROR "Source-backed #360 oracle FAILED (rc=${rc}):\n${stdout}\n${stderr}")
endif()
if(NOT EXISTS "${REPORT_FILE}")
  message(FATAL_ERROR "#360 atomic original-PDF report absent")
endif()
file(READ "${REPORT_FILE}" report)
foreach(marker IN ITEMS
    "\"source_kind\":\"test_generated_original_pdf\""
    "\"origin\":\"literal_pdf_bytes_not_reexport\""
    "\"decimal_contract_test_only\":true"
    "\"graph_exact_candidate_test_only\":true"
    "\"handoff_value_only_test\":true"
    "\"q4_consumer_only\":true"
    "\"correct_rounded_double_still_inflates\":true"
    "\"native_render\":\"not_run\""
    "\"public_sdk_backend_switched\":false"
    "\"external_native_positive_increment\":0")
  string(FIND "${report}" "${marker}" idx)
  if(idx EQUAL -1)
    message(FATAL_ERROR "Missing source/nonpromotion marker: ${marker}")
  endif()
endforeach()
string(JSON n LENGTH "${report}" cases)
string(JSON source_count GET "${report}" original_source_count)
string(JSON spelling_count GET "${report}" source_spelling_matches)
string(JSON repeat_count GET "${report}" pdfium_bitmap_repeat_count)
string(JSON frame_count GET "${report}" frame_count)
string(JSON integral_count GET "${report}" integer_span_pdfium_matches)
string(JSON unsupported_count GET "${report}" unsupported_dpi_zero_refusals)
string(JSON graph_exact_count GET "${report}" graph_exact_pdfium_matches)
string(JSON graph_exact_reject GET "${report}" graph_exact_unsupported_zero_refusals)
string(JSON graph_exact_bad_tokens GET "${report}" graph_exact_negative_lexer_refusals)
string(JSON graph_exact_budget GET "${report}" graph_exact_budget_zero_refusals)
string(JSON handoff_repeats GET "${report}" handoff_repeat_matches)
string(JSON handoff_survivors GET "${report}" handoff_after_graph_drop)
string(JSON handoff_corners GET "${report}" handoff_corner_edge_checks)
string(JSON handoff_old_unchanged GET "${report}" handoff_legacy_affine_equal)
string(JSON handoff_dpi_refusals GET "${report}" handoff_unsupported_dpi_zero)
string(JSON handoff_budget_refusals GET "${report}" handoff_budget_zero)
string(JSON handoff_bad_arguments GET "${report}" handoff_argument_zero)
string(JSON q4_frames GET "${report}" q4_consumer_frames)
string(JSON q4_samples GET "${report}" q4_consumer_valid_samples)
string(JSON q4_legacy GET "${report}" q4_legacy_bit_identical_samples)
string(JSON q4_different GET "${report}" q4_dimension_discrepant_samples)
string(JSON q4_sample_refused GET "${report}" q4_sample_zero_refusals)
string(JSON q4_work_ok GET "${report}" q4_work_acceptances)
string(JSON q4_work_refused GET "${report}" q4_work_budget_refusals)
string(JSON q4_edges_refused GET "${report}" q4_edge_budget_refusals)
if(NOT n EQUAL 14 OR NOT source_count EQUAL 14 OR
   NOT spelling_count EQUAL 56 OR NOT repeat_count EQUAL 42 OR
   NOT frame_count EQUAL 42 OR NOT integral_count EQUAL 36 OR
   NOT unsupported_count EQUAL 28 OR
   NOT graph_exact_count EQUAL 42 OR
   NOT graph_exact_reject EQUAL 28 OR
   NOT graph_exact_bad_tokens EQUAL 15 OR
   NOT graph_exact_budget EQUAL 28 OR
   NOT handoff_repeats EQUAL 42 OR
   NOT handoff_survivors EQUAL 42 OR
   NOT handoff_corners EQUAL 168 OR
   NOT handoff_old_unchanged EQUAL 30 OR
   NOT handoff_dpi_refusals EQUAL 28 OR
   NOT handoff_budget_refusals EQUAL 28 OR
   NOT handoff_bad_arguments EQUAL 28 OR
   NOT q4_frames EQUAL 42 OR
   NOT q4_samples EQUAL 3360 OR
   NOT q4_legacy EQUAL 2400 OR
   NOT q4_different EQUAL 960 OR
   NOT q4_sample_refused EQUAL 294 OR
   NOT q4_work_ok EQUAL 42 OR
   NOT q4_work_refused EQUAL 42 OR
   NOT q4_edges_refused EQUAL 42)
  message(FATAL_ERROR "#360 source/Graph/PDFium ledger missing cases")
endif()
set(dpis 72 96 144)
foreach(i RANGE 0 13)
  string(JSON nbytes GET "${report}" cases ${i} original_bytes)
  string(JSON fnv GET "${report}" cases ${i} source_fnv64)
  string(JSON crop GET "${report}" cases ${i} crop)
  string(JSON bitcount LENGTH "${report}" cases ${i} raw_crop_binary64)
  string(JSON frames LENGTH "${report}" cases ${i} frames)
  if(nbytes LESS 200 OR nbytes GREATER 65536 OR
     NOT frames EQUAL 3 OR NOT bitcount EQUAL 4 OR crop STREQUAL "")
    message(FATAL_ERROR "#360 incomplete source/Graph observation ${i}")
  endif()
  string(LENGTH "${fnv}" fnv_len)
  if(NOT fnv_len EQUAL 16 OR NOT fnv MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "#360 source byte identity fingerprint missing")
  endif()
  foreach(j RANGE 0 3)
    string(JSON bits GET "${report}" cases ${i} raw_crop_binary64 ${j})
    string(LENGTH "${bits}" bits_len)
    if(NOT bits_len EQUAL 16 OR NOT bits MATCHES "^[0-9a-f]+$")
      message(FATAL_ERROR "#360 raw binary64 endpoint evidence missing")
    endif()
  endforeach()
  foreach(j RANGE 0 2)
    list(GET dpis ${j} expected_dpi)
    string(JSON dpi GET "${report}" cases ${i} frames ${j} dpi)
    string(JSON ew GET "${report}" cases ${i} frames ${j} exact_grid 0)
    string(JSON eh GET "${report}" cases ${i} frames ${j} exact_grid 1)
    string(JSON pw GET "${report}" cases ${i} frames ${j} original_pdfium 0)
    string(JSON ph GET "${report}" cases ${i} frames ${j} original_pdfium 1)
    string(JSON gw GET "${report}" cases ${i} frames ${j} raw_graph_grid 0)
    string(JSON gh GET "${report}" cases ${i} frames ${j} raw_graph_grid 1)
    string(JSON xw GET "${report}" cases ${i} frames ${j} exact_graph_grid 0)
    string(JSON xh GET "${report}" cases ${i} frames ${j} exact_graph_grid 1)
    string(JSON hw GET "${report}" cases ${i} frames ${j} handoff_grid 0)
    string(JSON hh GET "${report}" cases ${i} frames ${j} handoff_grid 1)
    string(JSON q4_count GET "${report}" cases ${i} frames ${j} q4_consumer_samples)
    string(JSON q4_equal GET "${report}" cases ${i} frames ${j} q4_legacy_identical)
    string(JSON q4_mismatch GET "${report}" cases ${i} frames ${j} q4_size_discrepant)
    string(JSON q4_hash GET "${report}" cases ${i} frames ${j} q4_sample_fnv64)
    string(LENGTH "${q4_hash}" q4_hash_length)
    math(EXPR q4_sum "${q4_equal}+${q4_mismatch}")
    if(NOT q4_count EQUAL 80 OR
       NOT q4_sum EQUAL 80 OR
       NOT q4_hash_length EQUAL 16 OR
       NOT q4_hash MATCHES "^[0-9a-f]+$")
      message(FATAL_ERROR "#360 Q4 sample consumer observations incomplete")
    endif()
    string(JSON affine_len LENGTH "${report}" cases ${i} frames ${j} handoff_affine_hex)
    if(NOT affine_len EQUAL 6)
      message(FATAL_ERROR "#360 handoff affine value metadata incomplete")
    endif()
    foreach(a RANGE 0 5)
      string(JSON bits GET "${report}" cases ${i} frames ${j} handoff_affine_hex ${a})
      string(LENGTH "${bits}" bits_size)
      if(NOT bits_size EQUAL 16 OR NOT bits MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "#360 post-Snapshot affine bit-pattern missing")
      endif()
    endforeach()
    if(NOT xw EQUAL ew OR NOT xh EQUAL eh OR
       NOT xw EQUAL pw OR NOT xh EQUAL ph OR
       NOT hw EQUAL pw OR NOT hh EQUAL ph)
      message(FATAL_ERROR "#360 independent actual-Graph exact-decimal grid disagrees with source/PDFium")
    endif()
    if(NOT dpi EQUAL expected_dpi OR
       ew LESS 1 OR eh LESS 1 OR
       ew GREATER 500 OR eh GREATER 500 OR
       pw LESS 1 OR ph LESS 1 OR gw LESS 1 OR gh LESS 1)
      message(FATAL_ERROR "#360 invalid original Source/Grid frame")
    endif()
    if(i LESS 12)
      string(JSON parity GET "${report}" cases ${i} frames ${j} pdfium_matches_exact)
      if(NOT parity)
        message(FATAL_ERROR "#360 exact decimal span differs from original PDFium")
      endif()
    endif()
  endforeach()
endforeach()
message(STATUS
  "#360 source-backed decimal: 14 immutable test-owned literal PDFs, "
  "56 original Graph numeric spellings, 42 repeated PDFium frames, "
  "36 exact-integer-size PDFium matches, 28 hard unsupported DPI refuses, "
  "42/42 Graph-exact C11 dimensions and 15 lexical zero-output refusals; "
  "42/42 value-only handoffs after Graph drop, 168 affine corner checks; "
  "3360 private Q4 samples (2400 bit-identical legacy, 960 size-mismatch observations); "
  "no production parser or renderer mutation.")
