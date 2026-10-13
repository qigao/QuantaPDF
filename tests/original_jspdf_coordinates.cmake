# Included only inside QUANTAPDF_BUILD_TESTS, after the existing tests directory.
# Independent oracle: no QuantaPDF planner/backend/native renderer linked here.
add_executable(quantapdf_test_original_jspdf_coordinates
  "${PROJECT_SOURCE_DIR}/tests/test_original_jspdf_coordinates.c")
target_link_libraries(quantapdf_test_original_jspdf_coordinates PRIVATE QuantaPDF::PDFium)
set_target_properties(quantapdf_test_original_jspdf_coordinates PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
  target_compile_options(quantapdf_test_original_jspdf_coordinates PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_test_original_jspdf_coordinates PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
if(UNIX)
  target_link_libraries(quantapdf_test_original_jspdf_coordinates PRIVATE m)
endif()
add_test(NAME quantapdf.original_jspdf_pdfium_coordinates
  COMMAND "${CMAKE_COMMAND}"
    "-DORACLE=$<TARGET_FILE:quantapdf_test_original_jspdf_coordinates>"
    "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/external_wstar_candidates/documents/jspdf.w3s-clip-v1.pdf"
    -P "${PROJECT_SOURCE_DIR}/tests/check_original_jspdf_coordinates.cmake")
set_tests_properties(quantapdf.original_jspdf_pdfium_coordinates PROPERTIES
  TIMEOUT 20 LABELS "conformance;source-preflight;fractional-coordinate-oracle")
if(WIN32)
  set_property(TEST quantapdf.original_jspdf_pdfium_coordinates PROPERTY
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:quantapdf_pdfium>")
elseif(APPLE)
  set_property(TEST quantapdf.original_jspdf_pdfium_coordinates PROPERTY
    ENVIRONMENT_MODIFICATION "DYLD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:quantapdf_pdfium>")
else()
  set_property(TEST quantapdf.original_jspdf_pdfium_coordinates PROPERTY
    ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:quantapdf_pdfium>")
endif()
