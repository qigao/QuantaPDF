add_executable(quantapdf_test_backend_foundation test_backend_foundation.c)
target_include_directories(quantapdf_test_backend_foundation PRIVATE
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_backend_foundation PRIVATE
  quantapdf_backend
  QuantaPDF::PDFium)
target_compile_definitions(quantapdf_test_backend_foundation PRIVATE
  ONE_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  POSTER_BASIC_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/poster-basic.pdf")
add_test(NAME quantapdf.backend_foundation
  COMMAND quantapdf_test_backend_foundation)
set_tests_properties(quantapdf.backend_foundation PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_status test_status.c)
target_link_libraries(quantapdf_test_status PRIVATE QuantaPDF::QuantaPDF)
add_test(NAME quantapdf.status COMMAND quantapdf_test_status)

add_executable(quantapdf_test_composer
  test_composer.c
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer PRIVATE
  COMPOSER_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-output.pdf"
  COMPOSER_OUTPUT_SECOND_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-output-second.pdf")
add_test(NAME quantapdf.composer COMMAND quantapdf_test_composer)
set_tests_properties(quantapdf.composer PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_measurement
  test_composer_measurement.c
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_measurement PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_measurement PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_measurement PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_measurement PRIVATE
  ROBOTO_TTF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fonts/Roboto-Regular.ttf")
add_test(NAME quantapdf.composer_measurement
  COMMAND quantapdf_test_composer_measurement)
set_tests_properties(quantapdf.composer_measurement PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_affine
  test_composer_affine.cpp
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_affine PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_affine PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_affine PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_affine PRIVATE
  COMPOSER_AFFINE_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-affine-output.pdf"
  ROBOTO_TTF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fonts/Roboto-Regular.ttf")
add_test(NAME quantapdf.composer_affine COMMAND quantapdf_test_composer_affine)
set_tests_properties(quantapdf.composer_affine PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_vector
  test_composer_vector.c
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_vector PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_vector PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_vector PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_vector PRIVATE
  COMPOSER_VECTOR_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-vector-output.pdf")
add_test(NAME quantapdf.composer_vector COMMAND quantapdf_test_composer_vector)
set_tests_properties(quantapdf.composer_vector PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_dash
  test_composer_dash.c
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_dash PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_dash PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_dash PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_dash PRIVATE
  COMPOSER_DASH_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-dash-output.pdf")
add_test(NAME quantapdf.composer_dash COMMAND quantapdf_test_composer_dash)
set_tests_properties(quantapdf.composer_dash PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_raster
  test_composer_raster.c
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_raster PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_raster PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_raster PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_raster PRIVATE
  COMPOSER_RASTER_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-raster-output.pdf")
add_test(NAME quantapdf.composer_raster COMMAND quantapdf_test_composer_raster)
set_tests_properties(quantapdf.composer_raster PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_graphics_state
  test_composer_graphics_state.c
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_graphics_state PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_graphics_state PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_graphics_state PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_graphics_state PRIVATE
  COMPOSER_GRAPHICS_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-graphics-state-output.pdf"
  ROBOTO_TTF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fonts/Roboto-Regular.ttf")
add_test(NAME quantapdf.composer_graphics_state
  COMMAND quantapdf_test_composer_graphics_state)
set_tests_properties(quantapdf.composer_graphics_state PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_gradient
  test_composer_gradient.c
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_gradient PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_gradient PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_gradient PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_gradient PRIVATE
  COMPOSER_GRADIENT_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-gradient-output.pdf")
add_test(NAME quantapdf.composer_gradient COMMAND quantapdf_test_composer_gradient)
set_tests_properties(quantapdf.composer_gradient PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_clip
  test_composer_clip.c
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_clip PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_clip PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_clip PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_clip PRIVATE
  COMPOSER_CLIP_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-clip-output.pdf")
add_test(NAME quantapdf.composer_clip COMMAND quantapdf_test_composer_clip)
set_tests_properties(quantapdf.composer_clip PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_barcode
  test_composer_barcode.c
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_barcode PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_barcode PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_barcode PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_barcode PRIVATE
  COMPOSER_BARCODE_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-barcode-output.pdf")
add_test(NAME quantapdf.composer_barcode COMMAND quantapdf_test_composer_barcode)
set_tests_properties(quantapdf.composer_barcode PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_navigation
  test_composer_navigation.c)
target_link_libraries(quantapdf_test_composer_navigation PRIVATE
  QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_composer_navigation PRIVATE
  COMPOSER_NAVIGATION_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-navigation-output.pdf")
add_test(NAME quantapdf.composer_navigation
  COMMAND quantapdf_test_composer_navigation)
set_tests_properties(quantapdf.composer_navigation PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_font
  test_composer_font.c)
target_link_libraries(quantapdf_test_composer_font PRIVATE
  QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_composer_font PRIVATE
  COMPOSER_FONT_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-font-output.pdf"
  ROBOTO_TTF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fonts/Roboto-Regular.ttf")
add_test(NAME quantapdf.composer_font COMMAND quantapdf_test_composer_font)
set_tests_properties(quantapdf.composer_font PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_glyph_run
  test_composer_glyph_run.cpp
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_glyph_run PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_glyph_run PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_glyph_run PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_glyph_run PRIVATE
  COMPOSER_GLYPH_RUN_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-glyph-run-output.pdf"
  ROBOTO_TTF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fonts/Roboto-Regular.ttf")
add_test(NAME quantapdf.composer_glyph_run
  COMMAND quantapdf_test_composer_glyph_run)
set_tests_properties(quantapdf.composer_glyph_run PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_cff
  test_composer_cff.cpp
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_cff PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_cff PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_cff PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_cff PRIVATE
  CFF_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-cff-output.pdf"
  CFF2_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-cff2-output.pdf"
  SOURCE_SANS_CFF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fonts/SourceSans3-Regular.otf"
  SOURCE_SANS_CFF2="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fonts/SourceSans3VF-Upright.otf")
add_test(NAME quantapdf.composer_cff COMMAND quantapdf_test_composer_cff)
set_tests_properties(quantapdf.composer_cff PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_svg
  test_composer_svg.cpp
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_svg PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_svg PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_svg PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_svg PRIVATE
  COMPOSER_SVG_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-svg-output.pdf")
add_test(NAME quantapdf.composer_svg COMMAND quantapdf_test_composer_svg)
set_tests_properties(quantapdf.composer_svg PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_jpeg_validator
  test_jpeg_validator.c
  composer_test_helpers.cpp
  ../src/backend/jpeg_validator.c
  ../src/backend/jpeg_encoder.cpp)
target_include_directories(quantapdf_test_jpeg_validator PRIVATE
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(quantapdf_test_jpeg_validator PRIVATE
  QUANTAPDF_TESTING=1)
target_link_libraries(quantapdf_test_jpeg_validator PRIVATE
  qpdf::libqpdf
  JPEG::JPEG
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_jpeg_validator PRIVATE cxx_std_20)
add_test(NAME quantapdf.jpeg_validator
  COMMAND quantapdf_test_jpeg_validator)
set_tests_properties(quantapdf.jpeg_validator PROPERTIES TIMEOUT 10)

add_executable(quantapdf_test_jpeg_encoder
  test_jpeg_encoder.cpp
  ../src/backend/jpeg_encoder.cpp)
target_include_directories(quantapdf_test_jpeg_encoder PRIVATE
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_jpeg_encoder PRIVATE qpdf::libqpdf)
target_compile_features(quantapdf_test_jpeg_encoder PRIVATE cxx_std_20)
add_test(NAME quantapdf.jpeg_encoder COMMAND quantapdf_test_jpeg_encoder)
set_tests_properties(quantapdf.jpeg_encoder PROPERTIES TIMEOUT 10)

add_executable(quantapdf_test_pdf_image_recompression
  test_pdf_image_recompression.c
  test_pdf_image_recompression_semantics.c
  image_recompression_test_helpers.cpp
  pdf_image_recompression_fault_hook.c)
target_link_libraries(quantapdf_test_pdf_image_recompression PRIVATE
  QuantaPDF::QuantaPDF
  qpdf::libqpdf
  ZLIB::ZLIB)
target_include_directories(quantapdf_test_pdf_image_recompression PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_pdf_image_recompression PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_pdf_image_recompression PRIVATE
  QUANTAPDF_TESTING=1
  ONE_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  POSITIVE_SOURCE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/poster-basic.pdf"
  POSITIVE_FIXTURE_PDF="${CMAKE_CURRENT_BINARY_DIR}/image-recompression-positive.pdf"
  POSITIVE_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/image-recompression-positive-output.pdf"
  QUALITY40_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/image-recompression-quality40-output.pdf"
  EXPECTED_Q90_BASE64="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/image-recompression-expected-q90.pdf.base64"
  POLICY_FIXTURE_PDF="${CMAKE_CURRENT_BINARY_DIR}/image-recompression-policy.pdf"
  POLICY_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/image-recompression-policy-output.pdf"
  MALFORMED_FIXTURE_PDF="${CMAKE_CURRENT_BINARY_DIR}/image-recompression-malformed.pdf"
  SIGNED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation-signed.pdf"
  ENCRYPTED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/encrypted-one-page.pdf")
target_compile_definitions(quantapdf_test_pdf_image_recompression PRIVATE
  SEMANTIC_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/image-recompression-semantic-output.pdf"
  SEMANTIC_TEXT_SOURCE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/text-one-page.pdf"
  SEMANTIC_TEXT_PDF="${CMAKE_CURRENT_BINARY_DIR}/image-recompression-semantic-text.pdf"
  SEMANTIC_LINKS_SOURCE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-links.pdf"
  SEMANTIC_LINKS_PDF="${CMAKE_CURRENT_BINARY_DIR}/image-recompression-semantic-links.pdf"
  SEMANTIC_FORM_SOURCE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-choice.pdf"
  SEMANTIC_FORM_PDF="${CMAKE_CURRENT_BINARY_DIR}/image-recompression-semantic-form.pdf"
  SEMANTIC_OUTLINE_SOURCE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/outline-tree.pdf"
  SEMANTIC_OUTLINE_PDF="${CMAKE_CURRENT_BINARY_DIR}/image-recompression-semantic-outline.pdf"
  SEMANTIC_METADATA_SOURCE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/metadata-info.pdf"
  SEMANTIC_METADATA_PDF="${CMAKE_CURRENT_BINARY_DIR}/image-recompression-semantic-metadata.pdf")
add_test(NAME quantapdf.pdf_image_recompression
  COMMAND quantapdf_test_pdf_image_recompression)
set_tests_properties(quantapdf.pdf_image_recompression PROPERTIES TIMEOUT 60)

add_executable(quantapdf_test_version test_version.c)
target_link_libraries(quantapdf_test_version PRIVATE QuantaPDF::QuantaPDF)
add_test(NAME quantapdf.version COMMAND quantapdf_test_version)

add_executable(quantapdf_test_pdf_security
  test_pdf_security.c
  pdf_security_test_helpers.cpp)
target_link_libraries(quantapdf_test_pdf_security PRIVATE
  QuantaPDF::QuantaPDF
  qpdf::libqpdf)
target_compile_features(quantapdf_test_pdf_security PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_pdf_security PRIVATE
  SECURITY_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  SECURITY_SIGNED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation-signed.pdf"
  SECURITY_ENCRYPTED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/encrypted-one-page.pdf"
  SECURITY_FIXTURE_DIR="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME quantapdf.pdf_security COMMAND quantapdf_test_pdf_security)
set_tests_properties(quantapdf.pdf_security PROPERTIES TIMEOUT 60)

add_executable(quantapdf_test_pdf_security_rewrite
  test_pdf_security_rewrite.c
  test_pdf_security_rewrite_semantics.c
  pdf_security_rewrite_test_helpers.cpp
  pdf_security_rewrite_fault_hook.c)
find_package(Threads REQUIRED)
target_include_directories(quantapdf_test_pdf_security_rewrite PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_pdf_security_rewrite PRIVATE
  QuantaPDF::QuantaPDF
  qpdf::libqpdf
  Threads::Threads)
target_compile_features(quantapdf_test_pdf_security_rewrite PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_pdf_security_rewrite PRIVATE
  QUANTAPDF_TESTING=1)
if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(quantapdf_test_pdf_security_rewrite PRIVATE
    QUANTAPDF_SECURITY_STATIC_BUILD=1)
endif()
target_compile_definitions(quantapdf_test_pdf_security_rewrite PRIVATE
  SECURITY_PLAIN_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  SECURITY_SIGNED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation-signed.pdf"
  SECURITY_TEXT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/text-one-page.pdf"
  SECURITY_LEGACY_ENCRYPTED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/encrypted-one-page.pdf"
  SECURITY_ENCRYPTED_OUTPUT="${CMAKE_CURRENT_BINARY_DIR}/security-rewrite-encrypted.pdf"
  SECURITY_REENCRYPTED_OUTPUT="${CMAKE_CURRENT_BINARY_DIR}/security-rewrite-reencrypted.pdf"
  SECURITY_DECRYPTED_OUTPUT="${CMAKE_CURRENT_BINARY_DIR}/security-rewrite-decrypted.pdf")
target_compile_definitions(quantapdf_test_pdf_security_rewrite PRIVATE
  SECURITY_METADATA_FIXTURE="${CMAKE_CURRENT_BINARY_DIR}/security-rewrite-metadata.pdf"
  SECURITY_METADATA_ENCRYPTED_TRUE="${CMAKE_CURRENT_BINARY_DIR}/security-rewrite-metadata-true.pdf"
  SECURITY_METADATA_ENCRYPTED_FALSE="${CMAKE_CURRENT_BINARY_DIR}/security-rewrite-metadata-false.pdf")
target_compile_definitions(quantapdf_test_pdf_security_rewrite PRIVATE
  SECURITY_SEMANTIC_ENCRYPTED="${CMAKE_CURRENT_BINARY_DIR}/security-rewrite-semantic-encrypted.pdf"
  SECURITY_SEMANTIC_DECRYPTED="${CMAKE_CURRENT_BINARY_DIR}/security-rewrite-semantic-decrypted.pdf")
target_compile_definitions(quantapdf_test_pdf_security_rewrite PRIVATE
  SECURITY_SIGNATURE_FIXTURE="${CMAKE_CURRENT_BINARY_DIR}/security-rewrite-signature.pdf")
target_compile_definitions(quantapdf_test_pdf_security_rewrite PRIVATE
  SECURITY_ID_FIXTURE="${CMAKE_CURRENT_BINARY_DIR}/security-rewrite-id.pdf")
target_compile_definitions(quantapdf_test_pdf_security_rewrite PRIVATE
  SECURITY_SEMANTIC_TEXT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/text-one-page.pdf"
  SECURITY_SEMANTIC_LINKS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-links.pdf"
  SECURITY_SEMANTIC_IMAGES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-images.pdf"
  SECURITY_SEMANTIC_FORM_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-choice.pdf"
  SECURITY_SEMANTIC_OUTLINE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/outline-tree.pdf"
  SECURITY_SEMANTIC_METADATA_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/metadata-info.pdf"
  SECURITY_SEMANTIC_ANNOTATIONS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotations-mixed.pdf")
target_compile_definitions(quantapdf_test_pdf_security_rewrite PRIVATE
  SECURITY_SEMANTIC_CANONICAL="${CMAKE_CURRENT_BINARY_DIR}/security-rewrite-semantic-canonical.pdf")
add_test(NAME quantapdf.pdf_security_rewrite
  COMMAND quantapdf_test_pdf_security_rewrite)
set_tests_properties(quantapdf.pdf_security_rewrite PROPERTIES TIMEOUT 60)

add_executable(quantapdf_test_pdf_rewrite_lossless
  test_pdf_rewrite_lossless.c
  rewrite_test_helpers.cpp)
target_link_libraries(quantapdf_test_pdf_rewrite_lossless PRIVATE
  QuantaPDF::QuantaPDF
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_pdf_rewrite_lossless PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_pdf_rewrite_lossless PRIVATE
  ONE_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  TEXT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/text-one-page.pdf"
  OUTLINE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/outline-tree.pdf"
  LINKS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-links.pdf"
  ANNOTATIONS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotations-mixed.pdf"
  ACROFORM_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-choice.pdf"
  ENCRYPTED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/encrypted-one-page.pdf"
  SIGNED_FIELD_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation-signed.pdf"
  UNSIGNED_SIGNATURE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation-unsigned-signature.pdf"
  REPAIRABLE_BAD_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/outline-repairable-bad.pdf"
  REWRITE_GC_PDF="${CMAKE_CURRENT_BINARY_DIR}/rewrite-gc-input.pdf"
  REWRITE_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/rewrite-output.pdf"
  REWRITE_SEMANTIC_PDF="${CMAKE_CURRENT_BINARY_DIR}/rewrite-semantic.pdf"
  REWRITE_METADATA_PDF="${CMAKE_CURRENT_BINARY_DIR}/rewrite-metadata.pdf"
  REWRITE_VALIDATED_PDF="${CMAKE_CURRENT_BINARY_DIR}/rewrite-validated.pdf"
  REWRITE_SIGNED_PDF="${CMAKE_CURRENT_BINARY_DIR}/rewrite-signed.pdf"
  REWRITE_POLICY_PDF="${CMAKE_CURRENT_BINARY_DIR}/rewrite-policy.pdf"
  REWRITE_ROOT_PARENT_SIGNED_PDF="${CMAKE_CURRENT_BINARY_DIR}/rewrite-root-parent-signed.pdf"
  REWRITE_PARENT_MISMATCH_SIGNED_PDF="${CMAKE_CURRENT_BINARY_DIR}/rewrite-parent-mismatch-signed.pdf")
add_test(NAME quantapdf.pdf_rewrite_lossless
  COMMAND quantapdf_test_pdf_rewrite_lossless)
set_tests_properties(quantapdf.pdf_rewrite_lossless PROPERTIES TIMEOUT 30)

if(WIN32 AND BUILD_SHARED_LIBS)
  find_program(QUANTAPDF_DUMPBIN_EXECUTABLE NAMES dumpbin REQUIRED)
  add_test(NAME quantapdf.abi_exports
    COMMAND "${CMAKE_COMMAND}"
      "-DQUANTAPDF_DUMPBIN=${QUANTAPDF_DUMPBIN_EXECUTABLE}"
      "-DQUANTAPDF_LIBRARY=$<TARGET_FILE:quantapdf>"
      "-DQUANTAPDF_EXPORT_BASELINE=${CMAKE_CURRENT_SOURCE_DIR}/../abi/quantapdf-v2.exports"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CheckQuantaPDFExports.cmake")
  set_tests_properties(quantapdf.abi_exports PROPERTIES TIMEOUT 10)

  add_executable(quantapdf_test_fake_dumpbin fake_dumpbin.c)
  add_test(NAME quantapdf.abi_checker_rejects_extra_export
    COMMAND "${CMAKE_COMMAND}"
      "-DQUANTAPDF_DUMPBIN=$<TARGET_FILE:quantapdf_test_fake_dumpbin>"
      "-DQUANTAPDF_LIBRARY=${CMAKE_CURRENT_SOURCE_DIR}/../abi/quantapdf-v2.exports"
      "-DQUANTAPDF_EXPORT_BASELINE=${CMAKE_CURRENT_SOURCE_DIR}/../abi/quantapdf-v2.exports"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CheckQuantaPDFExports.cmake")
  set_tests_properties(quantapdf.abi_checker_rejects_extra_export PROPERTIES
    TIMEOUT 10
    WILL_FAIL TRUE)
endif()
set_tests_properties(quantapdf.status PROPERTIES TIMEOUT 10)

set(UTF8_PDF "${CMAKE_CURRENT_BINARY_DIR}/quantapdf-测试.pdf")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  "${UTF8_PDF}"
  COPYONLY)

add_executable(quantapdf_test_document test_document.c)
target_link_libraries(quantapdf_test_document PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_document PRIVATE
  ONE_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  TWO_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/two-page.pdf"
  PAGE_BOXES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-boxes.pdf"
  ENCRYPTED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/encrypted-one-page.pdf"
  NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt"
  TRUNCATED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/truncated.pdf"
  MISSING_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/does-not-exist.pdf"
  UTF8_PDF="${UTF8_PDF}")
if(MSVC)
  target_compile_options(quantapdf_test_document PRIVATE /utf-8)
endif()
add_test(NAME quantapdf.document COMMAND quantapdf_test_document)
set_tests_properties(quantapdf.document PROPERTIES TIMEOUT 60)

add_executable(quantapdf_test_render test_render.c)
target_link_libraries(quantapdf_test_render PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_render PRIVATE
  ONE_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  PAGE_BOXES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-boxes.pdf")
add_test(NAME quantapdf.render COMMAND quantapdf_test_render)
set_tests_properties(quantapdf.render PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_text test_text.c)
target_link_libraries(quantapdf_test_text PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_text PRIVATE
  ONE_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  TEXT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/text-one-page.pdf")
add_test(NAME quantapdf.text COMMAND quantapdf_test_text)
set_tests_properties(quantapdf.text PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_structured_text test_structured_text.c)
target_link_libraries(quantapdf_test_structured_text PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_structured_text PRIVATE
  ONE_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  STRUCTURED_TEXT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/structured-text.pdf")
add_test(NAME quantapdf.structured_text COMMAND quantapdf_test_structured_text)
set_tests_properties(quantapdf.structured_text PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_text_search test_text_search.c)
target_link_libraries(quantapdf_test_text_search PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_text_search PRIVATE
  STRUCTURED_TEXT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/structured-text.pdf")
add_test(NAME quantapdf.text_search COMMAND quantapdf_test_text_search)
set_tests_properties(quantapdf.text_search PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_images test_images.c)
target_link_libraries(quantapdf_test_images PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_images PRIVATE
  ONE_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  PAGE_IMAGES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-images.pdf")
add_test(NAME quantapdf.images COMMAND quantapdf_test_images)
set_tests_properties(quantapdf.images PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_image_bitmap test_image_bitmap.c)
target_link_libraries(quantapdf_test_image_bitmap PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_image_bitmap PRIVATE
  PAGE_IMAGES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-images.pdf"
  PAGE_IMAGES_ALPHA_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-images-alpha.pdf")
add_test(NAME quantapdf.image_bitmap COMMAND quantapdf_test_image_bitmap)
set_tests_properties(quantapdf.image_bitmap PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_links test_links.c)
target_link_libraries(quantapdf_test_links PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_links PRIVATE
  PAGE_LINKS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-links.pdf")
add_test(NAME quantapdf.links COMMAND quantapdf_test_links)
set_tests_properties(quantapdf.links PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_pdf_export test_pdf_export.c)
target_link_libraries(quantapdf_test_pdf_export PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_export PRIVATE
  COMPOSITION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  COMPOSITION_NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt"
  COMPOSITION_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composition-export-output.pdf")
add_test(NAME quantapdf.pdf_export COMMAND quantapdf_test_pdf_export)
set_tests_properties(quantapdf.pdf_export PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_pdf_range test_pdf_range.c)
target_link_libraries(quantapdf_test_pdf_range PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_range PRIVATE
  COMPOSITION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  COMPOSITION_NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt"
  RANGE_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composition-range-output.pdf")
add_test(NAME quantapdf.pdf_range COMMAND quantapdf_test_pdf_range)
set_tests_properties(quantapdf.pdf_range PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_pdf_order test_pdf_order.c)
target_link_libraries(quantapdf_test_pdf_order PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_order PRIVATE
  COMPOSITION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  ORDER_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composition-order-output.pdf")
add_test(NAME quantapdf.pdf_order COMMAND quantapdf_test_pdf_order)
set_tests_properties(quantapdf.pdf_order PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_pdf_delete test_pdf_delete.c)
target_link_libraries(quantapdf_test_pdf_delete PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_delete PRIVATE
  COMPOSITION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  DELETE_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composition-delete-output.pdf")
add_test(NAME quantapdf.pdf_delete COMMAND quantapdf_test_pdf_delete)
set_tests_properties(quantapdf.pdf_delete PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_pdf_merge test_pdf_merge.c)
target_link_libraries(quantapdf_test_pdf_merge PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_merge PRIVATE
  COMPOSITION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  TEXT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/text-one-page.pdf"
  MERGE_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composition-merge-output.pdf")
add_test(NAME quantapdf.pdf_merge COMMAND quantapdf_test_pdf_merge)
set_tests_properties(quantapdf.pdf_merge PROPERTIES TIMEOUT 30)

set(OUTPUT_FILE_ASCII "${CMAKE_CURRENT_BINARY_DIR}/composition-save-output.pdf")
set(OUTPUT_FILE_UTF8 "${CMAKE_CURRENT_BINARY_DIR}/composition-save-测试.pdf")
set(OUTPUT_FILE_MISSING_PARENT_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/quantapdf-save-missing-parent")
set(OUTPUT_FILE_MISSING_PARENT
  "${OUTPUT_FILE_MISSING_PARENT_DIR}/output.pdf")

add_executable(quantapdf_test_output_file test_output_file.c)
target_link_libraries(quantapdf_test_output_file PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_output_file PRIVATE
  COMPOSITION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  ASCII_OUTPUT_PDF="${OUTPUT_FILE_ASCII}"
  UTF8_OUTPUT_PDF="${OUTPUT_FILE_UTF8}"
  MISSING_PARENT_DIR="${OUTPUT_FILE_MISSING_PARENT_DIR}"
  MISSING_PARENT_PDF="${OUTPUT_FILE_MISSING_PARENT}")
if(MSVC)
  target_compile_options(quantapdf_test_output_file PRIVATE /utf-8)
endif()
add_test(NAME quantapdf.output_file COMMAND quantapdf_test_output_file)
set_tests_properties(quantapdf.output_file PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_pdf_metadata test_pdf_metadata.c)
target_link_libraries(quantapdf_test_pdf_metadata PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_metadata PRIVATE
  METADATA_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/metadata-info.pdf"
  COMPOSITION_NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt")
add_test(NAME quantapdf.pdf_metadata COMMAND quantapdf_test_pdf_metadata)
set_tests_properties(quantapdf.pdf_metadata PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_pdf_outline test_pdf_outline.c)
target_link_libraries(quantapdf_test_pdf_outline PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_outline PRIVATE
  OUTLINE_TREE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/outline-tree.pdf"
  OUTLINE_REPAIRABLE_BAD_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/outline-repairable-bad.pdf"
  OUTLINE_CYCLE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/outline-cycle.pdf"
  OUTLINE_DEPTH_257_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/outline-depth-257.pdf"
  EMPTY_OUTLINE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  COMPOSITION_NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt")
add_test(NAME quantapdf.pdf_outline COMMAND quantapdf_test_pdf_outline)
set_tests_properties(quantapdf.pdf_outline PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_pdf_annotations test_pdf_annotations.c)
target_link_libraries(quantapdf_test_pdf_annotations PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_annotations PRIVATE
  ANNOTATIONS_MIXED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotations-mixed.pdf"
  ANNOTATIONS_NONARRAY_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotations-nonarray.pdf"
  ANNOTATIONS_FILTERED_ONLY_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotations-filtered-only.pdf"
  ANNOTATIONS_LATE_MALFORMED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotations-late-malformed.pdf"
  EMPTY_ANNOTATIONS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf")
add_test(NAME quantapdf.pdf_annotations COMMAND quantapdf_test_pdf_annotations)
set_tests_properties(quantapdf.pdf_annotations PROPERTIES TIMEOUT 30)

set(MUTATION_OUTPUT_A "${CMAKE_CURRENT_BINARY_DIR}/annotation-mutation-a.pdf")
set(MUTATION_OUTPUT_B "${CMAKE_CURRENT_BINARY_DIR}/annotation-mutation-b.pdf")
set(MUTATION_OUTPUT_JS "${CMAKE_CURRENT_BINARY_DIR}/annotation-mutation-js-output.pdf")

add_executable(quantapdf_test_pdf_annotation_mutation
  test_pdf_annotation_mutation.c
  pdf_edit_fault_hook.c)
target_link_libraries(quantapdf_test_pdf_annotation_mutation PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_annotation_mutation PRIVATE
  QUANTAPDF_TESTING=1
  MUTATION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation.pdf"
  SIGNED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation-signed.pdf"
  UNSIGNED_SIGNATURE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation-unsigned-signature.pdf"
  JS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation-js.pdf"
  LATE_MALFORMED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotations-late-malformed.pdf"
  ENCRYPTED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/encrypted-one-page.pdf"
  NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt"
  OUTPUT_A_PDF="${MUTATION_OUTPUT_A}"
  OUTPUT_B_PDF="${MUTATION_OUTPUT_B}"
  OUTPUT_JS_PDF="${MUTATION_OUTPUT_JS}")
add_test(NAME quantapdf.pdf_annotation_mutation
  COMMAND quantapdf_test_pdf_annotation_mutation)
set_tests_properties(quantapdf.pdf_annotation_mutation PROPERTIES TIMEOUT 60)

add_executable(quantapdf_test_pdf_form test_pdf_form.c)
target_link_libraries(quantapdf_test_pdf_form PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_form PRIVATE
  ACROFORM_STRUCTURE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-structure.pdf"
  ACROFORM_WIDGETS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-widgets.pdf"
  ACROFORM_SCALARS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-scalars.pdf"
  ACROFORM_CHOICE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-choice.pdf"
  ACROFORM_MAIN_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-main.pdf"
  ACROFORM_NO_FIELDS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-no-fields.pdf"
  ACROFORM_EMPTY_FIELDS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-empty-fields.pdf"
  ACROFORM_ANNOTS_NONARRAY_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-annots-nonarray.pdf"
  ACROFORM_JS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-js.pdf"
  ACROFORM_STREAM_VALUE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-stream-value.pdf"
  ACROFORM_BAD_ROOT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-root.pdf"
  ACROFORM_BAD_FIELDS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-fields.pdf"
  ACROFORM_BAD_KID_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-kid.pdf"
  ACROFORM_CYCLE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-cycle.pdf"
  ACROFORM_REPEATED_NODE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-repeated-node.pdf"
  ACROFORM_PARENT_MISMATCH_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-parent-mismatch.pdf"
  ACROFORM_ROOT_PARENT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-root-parent.pdf"
  ACROFORM_DEPTH_257_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-depth-257.pdf"
  ACROFORM_MIXED_GROUP_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mixed-group.pdf"
  ACROFORM_GROUP_CONFLICT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-group-conflict.pdf"
  ACROFORM_DUPLICATE_NAME_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-duplicate-name.pdf"
  ACROFORM_PERIOD_NAME_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-period-name.pdf"
  ACROFORM_BAD_FT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-ft.pdf"
  ACROFORM_BAD_FF_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-ff.pdf"
  ACROFORM_BAD_VALUE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-value.pdf"
  ACROFORM_BAD_OPT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-opt.pdf"
  ACROFORM_BAD_I_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-i.pdf"
  ACROFORM_BAD_BUTTON_AP_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-button-ap.pdf"
  ACROFORM_ORPHAN_WIDGET_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-orphan-widget.pdf"
  ACROFORM_MISSING_WIDGET_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-missing-widget.pdf"
  ACROFORM_DUPLICATE_WIDGET_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-duplicate-widget.pdf"
  ACROFORM_P_MISMATCH_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-p-mismatch.pdf"
  ACROFORM_DIRECT_WIDGET_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-direct-widget.pdf"
  ACROFORM_BAD_WIDGET_RECT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-widget-rect.pdf"
  ACROFORM_BAD_WIDGET_FLAGS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-widget-flags.pdf"
  ACROFORM_BAD_SIGNATURE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-signature.pdf"
  ACROFORM_LATE_MALFORMED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-late-malformed.pdf"
  NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt"
  NO_ACROFORM_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf")
add_test(NAME quantapdf.pdf_form COMMAND quantapdf_test_pdf_form)
set_tests_properties(quantapdf.pdf_form PROPERTIES TIMEOUT 60)

add_executable(quantapdf_test_pdf_form_mutation
  test_pdf_form_mutation.c
  test_pdf_form_appearance.c
  test_pdf_form_mutation_main.c
  pdf_edit_fault_hook.c)
set_source_files_properties(test_pdf_form_mutation.c PROPERTIES
  COMPILE_DEFINITIONS "main=quantapdf_pdf_form_mutation_base_main")
target_link_libraries(quantapdf_test_pdf_form_mutation PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_form_mutation PRIVATE
  QUANTAPDF_TESTING=1
  FORM_MUTATION_BASIC_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-basic.pdf"
  FORM_MUTATION_CHOICE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-choice.pdf"
  FORM_MUTATION_GROUPS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-groups-pdfium.pdf"
  FORM_MUTATION_MODES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-modes-pdfium.pdf"
  FORM_MUTATION_EVENTS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-events.pdf"
  FORM_MUTATION_NEED_APPEARANCES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-need-appearances.pdf"
  FORM_MUTATION_XFA_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-xfa.pdf"
  FORM_MUTATION_BAD_NEED_APPEARANCES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-bad-need-appearances.pdf"
  FORM_MUTATION_DIRECT_FIELD_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-direct-field-pdfium.pdf")
add_test(NAME quantapdf.pdf_form_mutation
  COMMAND quantapdf_test_pdf_form_mutation)
set_tests_properties(quantapdf.pdf_form_mutation PROPERTIES TIMEOUT 60)

add_executable(quantapdf_test_pdf_crop
  test_pdf_crop.c
  qpdf_test_helpers.cpp
  test_pdf_crop_transforms.c)
set_source_files_properties(test_pdf_crop.c PROPERTIES
  COMPILE_DEFINITIONS "main=quantapdf_pdf_crop_base_main")
target_link_libraries(quantapdf_test_pdf_crop PRIVATE
  QuantaPDF::QuantaPDF
  qpdf::libqpdf)
target_compile_features(quantapdf_test_pdf_crop PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_pdf_crop PRIVATE
  CROP_INTERACTIVE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/crop-interactive.pdf"
  CROP_INHERITED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/crop-inherited.pdf"
  CROP_ROTATE_90_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/crop-rotate-90.pdf"
  CROP_USERUNIT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/crop-userunit.pdf"
  CROP_OUTSIDE_MEDIA_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/crop-cropbox-outside-media.pdf"
  CROP_MALFORMED_BOX_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/crop-malformed-box.pdf"
  CROP_MALFORMED_ROTATE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/crop-malformed-rotate.pdf"
  CROP_MALFORMED_USERUNIT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/crop-malformed-userunit.pdf"
  SIGNED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation-signed.pdf"
  ENCRYPTED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/encrypted-one-page.pdf"
  NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt"
  CROP_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/crop-output.pdf")
add_test(NAME quantapdf.pdf_crop COMMAND quantapdf_test_pdf_crop)
set_tests_properties(quantapdf.pdf_crop PROPERTIES TIMEOUT 60)

add_executable(quantapdf_test_pdf_trim test_pdf_trim.c)
target_link_libraries(quantapdf_test_pdf_trim PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_trim PRIVATE
  TRIM_INTERACTIVE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/trim-interactive.pdf")
add_test(NAME quantapdf.pdf_trim COMMAND quantapdf_test_pdf_trim)
set_tests_properties(quantapdf.pdf_trim PROPERTIES TIMEOUT 60)

add_executable(quantapdf_test_pdf_poster_split
  test_pdf_poster_split.c
  pdf_poster_fault_hook.c)
target_link_libraries(quantapdf_test_pdf_poster_split PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_poster_split PRIVATE
  QUANTAPDF_TESTING=1
  POSTER_BASIC_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/poster-basic.pdf")
add_test(NAME quantapdf.pdf_poster_split COMMAND quantapdf_test_pdf_poster_split)
set_tests_properties(quantapdf.pdf_poster_split PROPERTIES TIMEOUT 60)

add_executable(quantapdf_test_pdf_flatten
  test_pdf_flatten.c
  flatten_test_helpers.cpp)
target_link_libraries(quantapdf_test_pdf_flatten PRIVATE
  QuantaPDF::QuantaPDF
  qpdf::libqpdf)
target_compile_features(quantapdf_test_pdf_flatten PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_pdf_flatten PRIVATE
  FLATTEN_BASIC_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-basic.pdf"
  FLATTEN_NO_ANNOTATIONS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  FLATTEN_APPEARANCE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-appearance.pdf"
  FLATTEN_APPEARANCE_MALFORMED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-appearance-malformed.pdf"
  FLATTEN_CONTENTS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-contents.pdf"
  FLATTEN_CONTENTS_MALFORMED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-contents-malformed.pdf"
  FLATTEN_FLAG_ISOLATION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-flag-isolation.pdf"
  FLATTEN_NUMBER_FORMAT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-number-format.pdf"
  FLATTEN_WIDGETS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-widgets.pdf"
  FLATTEN_WIDGET_AS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-widget-as.pdf"
  FLATTEN_WIDGET_RADIO_AS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-widget-radio-as.pdf"
  FLATTEN_COMBINED_ORDER_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-combined-order.pdf"
  FLATTEN_FORM_COW_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-form-cow.pdf"
  FLATTEN_FORM_KIDS_COW_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-form-kids-cow.pdf"
  FLATTEN_FORM_CO_COW_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-form-co-cow.pdf"
  FLATTEN_FORM_CLOSURE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-form-closure.pdf"
  FLATTEN_FORM_ANCESTOR_SURVIVES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-form-ancestor-survives.pdf"
  FLATTEN_MULTI_WIDGET_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-multi-widget.pdf"
  FLATTEN_FORM_MULTI_ROOT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-form-multi-root.pdf"
  FLATTEN_FORM_DEEP_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-form-deep.pdf"
  FLATTEN_FORM_DEEP_SURVIVOR_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-form-deep-survivor.pdf"
  FLATTEN_REDACT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-subtype-redact.pdf"
  FLATTEN_LINK_DEFAULT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-link-default.pdf"
  FLATTEN_TAGGED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-tagged.pdf"
  FLATTEN_TAGGED_NOOP_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-tagged-noop.pdf"
  FLATTEN_NEED_APPEARANCES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-needappearances-true.pdf"
  FLATTEN_XFA_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-xfa.pdf"
  FLATTEN_ANNOTATION_WITH_WIDGET_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-annotation-with-widget.pdf"
  FLATTEN_LINK_BS0_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-link-bs0.pdf"
  FLATTEN_LINK_BORDER0_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-link-border0.pdf"
  FLATTEN_LINK_BS0_BORDER4_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-link-bs0-border4.pdf"
  FLATTEN_LINK_BS4_BORDER0_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-link-bs4-border0.pdf"
  FLATTEN_LINK_BS_MISSING_W_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-link-bs-missing-w.pdf"
  FLATTEN_LINK_APN_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-link-apn.pdf"
  FLATTEN_LINK_BS_MALFORMED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-link-bs-malformed.pdf"
  FLATTEN_LINK_BAD_W_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-link-bad-w.pdf"
  FLATTEN_LINK_BAD_BORDER_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-link-bad-border.pdf"
  FLATTEN_POPUP_PARENT_REVERSE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-popup-parent-reverse.pdf"
  FLATTEN_POPUP_DIRECT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-popup-direct.pdf"
  FLATTEN_IRT_FORWARD_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-irt-forward.pdf"
  FLATTEN_IRT_REVERSE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-irt-reverse.pdf"
  FLATTEN_FILE_ATTACHMENT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-subtype-file-attachment.pdf"
  FLATTEN_RICH_MEDIA_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-subtype-rich-media.pdf"
  FLATTEN_UNKNOWN_SUBTYPE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-subtype-unknown.pdf"
  FLATTEN_NEED_APPEARANCES_MALFORMED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-needappearances-malformed.pdf"
  FLATTEN_NEED_APPEARANCES_FALSE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-policy-needappearances-false.pdf"
  FLATTEN_NOOP_NEUTRAL_LINK_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-noop-neutral-link.pdf"
  FLATTEN_NOOP_ZERO_WIDGET_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-noop-zero-widget.pdf"
  FLATTEN_NOOP_MALFORMED_ANNOTS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-noop-malformed-annots.pdf"
  FLATTEN_NOOP_MALFORMED_ACROFORM_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-noop-malformed-acroform.pdf"
  FLATTEN_WIDGET_P_MISMATCH_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-p-mismatch.pdf"
  FLATTEN_ENCRYPTED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/encrypted-one-page.pdf"
  FLATTEN_SIGNED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotation-mutation-signed.pdf"
  FLATTEN_BAD_APPEARANCE_STREAM_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-bad-appearance-stream.pdf"
  FLATTEN_BAD_PAGE_CONTENT_STREAM_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-bad-page-content-stream.pdf"
  FLATTEN_TRUNCATED_APPEARANCE_STREAM_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-truncated-appearance-stream.pdf"
  FLATTEN_BAD_APPEARANCE_OPERATOR_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-bad-appearance-operator.pdf"
  FLATTEN_UNBALANCED_PAGE_Q_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-unbalanced-page-q.pdf"
  FLATTEN_UNBALANCED_PAGE_TEXT_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-unbalanced-page-text.pdf"
  FLATTEN_VALID_WIDGET_P_MISMATCH_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-valid-widget-p-mismatch.pdf"
  FLATTEN_BAD_APPEARANCE_ARITY_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-bad-appearance-arity.pdf"
  FLATTEN_BAD_PAGE_OPERAND_TYPE_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-bad-page-operand-type.pdf"
  FLATTEN_BAD_APPEARANCE_CONTEXT_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-bad-appearance-context.pdf"
  FLATTEN_BAD_PAGE_PATH_CONTEXT_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-bad-page-path-context.pdf"
  FLATTEN_BAD_PAGE_TEXT_PATH_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-bad-page-text-path.pdf"
  FLATTEN_VALID_INLINE_APPEARANCE_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-valid-inline-appearance.pdf"
  FLATTEN_VALID_TEXT_GSTATE_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-valid-text-gstate.pdf"
  FLATTEN_CROSSED_SCOPES_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-crossed-scopes.pdf"
  FLATTEN_CA_HALF_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-ca-half.pdf"
  FLATTEN_CA_MALFORMED_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-ca-malformed.pdf"
  FLATTEN_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/flatten-output.pdf")
add_test(NAME quantapdf.pdf_flatten COMMAND quantapdf_test_pdf_flatten)
set_tests_properties(quantapdf.pdf_flatten PROPERTIES TIMEOUT 60)

get_property(_quantapdf_tests DIRECTORY PROPERTY TESTS)
if(WIN32)
  set_property(TEST ${_quantapdf_tests} PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST ${_quantapdf_tests} PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST ${_quantapdf_tests} PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()
