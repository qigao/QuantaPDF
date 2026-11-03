execute_process(
  COMMAND "${QUANTAPDF_LLVM_READOBJ}" --coff-exports "${QUANTAPDF_LIBRARY}"
  OUTPUT_VARIABLE output
  COMMAND_ERROR_IS_FATAL ANY)

file(STRINGS "${QUANTAPDF_EXPORT_MANIFEST}" expected
  REGEX "^quantapdf_[A-Za-z0-9_]+$")
list(SORT expected)
list(LENGTH expected expected_count)

# The manifest names the current header, not a previous SDK's exports.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../include/quantapdf/quantapdf.h" header)
string(REGEX MATCHALL
  "QUANTAPDF_API[ \t\r\n]+[A-Za-z_][A-Za-z0-9_ *\t\r\n]+quantapdf_[A-Za-z0-9_]+[ \t\r\n]*\\("
  declarations "${header}")
set(declared)
foreach(declaration IN LISTS declarations)
  string(REGEX MATCH "quantapdf_[A-Za-z0-9_]+[ \t\r\n]*\\(" name "${declaration}")
  string(REGEX REPLACE "[ \t\r\n]*\\($" "" name "${name}")
  list(APPEND declared "${name}")
endforeach()
list(SORT declared)
if(NOT "${declared}" STREQUAL "${expected}")
  message(FATAL_ERROR "QuantaPDF export manifest differs from the current header")
endif()

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
    "QuantaPDF export count mismatch\n"
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
    "QuantaPDF export mismatch\n"
    "Missing: ${missing}\n"
    "Unexpected: ${unexpected}")
endif()
