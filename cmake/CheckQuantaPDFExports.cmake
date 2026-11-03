foreach(required IN ITEMS
    QUANTAPDF_LLVM_READOBJ
    QUANTAPDF_LIBRARY
    QUANTAPDF_EXPORT_BASELINE)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

execute_process(
  COMMAND "${QUANTAPDF_LLVM_READOBJ}" --coff-exports "${QUANTAPDF_LIBRARY}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "llvm-readobj failed: ${error}")
endif()

file(STRINGS "${QUANTAPDF_EXPORT_BASELINE}" expected
  REGEX "^quantapdf_[A-Za-z0-9_]+$")
list(REMOVE_DUPLICATES expected)
list(SORT expected)
list(LENGTH expected expected_count)

# Count every export record, including unnamed or duplicate-name entries.
# Comparing a deduplicated name list alone could hide extra ordinal exports.
string(REGEX MATCHALL "Export \\{" exports "${output}")
list(LENGTH exports export_count)
string(REPLACE "\r\n" "\n" output_lines "${output}")
string(REPLACE "\r" "\n" output_lines "${output_lines}")
string(REPLACE "\n" ";" output_lines "${output_lines}")
set(actual)
foreach(line IN LISTS output_lines)
  if(line MATCHES "^[ \t]*Name: ([^ \t]+)[ \t]*$")
    list(APPEND actual "${CMAKE_MATCH_1}")
  endif()
endforeach()
list(LENGTH actual name_count)
list(SORT actual)

if(NOT export_count EQUAL expected_count OR NOT name_count EQUAL expected_count)
  message(FATAL_ERROR
    "QuantaPDF ABI export count mismatch\n"
    "Expected named functions: ${expected_count}\n"
    "Exports: ${export_count}\n"
    "Names: ${name_count}")
endif()

if(NOT "${actual}" STREQUAL "${expected}")
  set(unexpected ${actual})
  list(REMOVE_ITEM unexpected ${expected})
  set(missing ${expected})
  list(REMOVE_ITEM missing ${actual})
  message(FATAL_ERROR
    "QuantaPDF ABI export mismatch\n"
    "Missing: ${missing}\n"
    "Unexpected: ${unexpected}")
endif()
