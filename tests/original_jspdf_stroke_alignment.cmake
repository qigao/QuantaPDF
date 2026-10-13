# Test-only, direct PDFium scanline A/B vs real private C11 device-grid samples.
# This target does not use QuantaPDF's runtime renderer or a PDFium fallback.
add_executable(quantapdf_test_original_jspdf_stroke_alignment
  "${PROJECT_SOURCE_DIR}/tests/test_original_jspdf_stroke_alignment.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_grid.c")
target_include_directories(quantapdf_test_original_jspdf_stroke_alignment
  PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_original_jspdf_stroke_alignment
  PRIVATE QuantaPDF::PDFium)
set_target_properties(quantapdf_test_original_jspdf_stroke_alignment PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
  target_compile_options(quantapdf_test_original_jspdf_stroke_alignment
    PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_test_original_jspdf_stroke_alignment
    PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(UNIX)
  target_link_libraries(quantapdf_test_original_jspdf_stroke_alignment PRIVATE m)
endif()
add_test(NAME quantapdf.original_jspdf_stroke_alignment
  COMMAND "${CMAKE_COMMAND}"
    "-DORACLE=$<TARGET_FILE:quantapdf_test_original_jspdf_stroke_alignment>"
    "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/external_wstar_candidates/documents/jspdf.w3s-clip-v1.pdf"
    "-DREPORT=${CMAKE_CURRENT_BINARY_DIR}/original-jspdf-stroke-phase-ab.json"
    -P "${PROJECT_SOURCE_DIR}/tests/check_original_jspdf_stroke_alignment.cmake")
set_tests_properties(quantapdf.original_jspdf_stroke_alignment PROPERTIES
  TIMEOUT 40
  LABELS "conformance;source-preflight;fractional-coordinate-oracle;stroke-alignment")
if(WIN32)
  set_property(TEST quantapdf.original_jspdf_stroke_alignment PROPERTY
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:quantapdf_pdfium>")
elseif(APPLE)
  set_property(TEST quantapdf.original_jspdf_stroke_alignment PROPERTY
    ENVIRONMENT_MODIFICATION "DYLD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:quantapdf_pdfium>")
else()
  set_property(TEST quantapdf.original_jspdf_stroke_alignment PROPERTY
    ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:quantapdf_pdfium>")
endif()
