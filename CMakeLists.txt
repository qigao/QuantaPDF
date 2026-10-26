cmake_minimum_required(VERSION 3.20)
project(QuantaPDF VERSION 2.34.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(QUANTAPDF_BUILD_TESTS "Build QuantaPDF tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(QuantaPDFExports)
include(QuantaPDFPdfium)
quantapdf_import_pdfium()

find_package(qpdf 12.4.0 EXACT CONFIG REQUIRED)
if(NOT TARGET qpdf::libqpdf)
  message(FATAL_ERROR "qpdf did not provide qpdf::libqpdf")
endif()
find_package(ZLIB REQUIRED)
find_package(JPEG REQUIRED)
# qpdf 12.4 exports its static dependencies as bare library names. Normalize
# them to package targets so consumers do not depend on ambient linker paths.
set_property(TARGET qpdf::libqpdf PROPERTY INTERFACE_LINK_LIBRARIES
  "ZLIB::ZLIB;JPEG::JPEG")
set(QUANTAPDF_QPDF_COPYRIGHT "${qpdf_DIR}/copyright")
if(NOT EXISTS "${QUANTAPDF_QPDF_COPYRIGHT}")
  message(FATAL_ERROR
    "qpdf package copyright notice is missing: ${QUANTAPDF_QPDF_COPYRIGHT}")
endif()
get_filename_component(
  QUANTAPDF_VCPKG_SHARE_ROOT "${qpdf_DIR}" DIRECTORY)
set(QUANTAPDF_LIBJPEG_TURBO_COPYRIGHT
  "${QUANTAPDF_VCPKG_SHARE_ROOT}/libjpeg-turbo/copyright")
if(NOT EXISTS "${QUANTAPDF_LIBJPEG_TURBO_COPYRIGHT}")
  message(FATAL_ERROR
    "libjpeg-turbo package copyright notice is missing: "
    "${QUANTAPDF_LIBJPEG_TURBO_COPYRIGHT}")
endif()

# Canonical private C11 Source mixed raster objects: compiled ONCE and
# embedded in the product library or standalone original-source tests.
# No qpdf/PDFium headers or runtime links, no installed/exported target.
add_library(quantapdf_native_raster_objects OBJECT
  "${CMAKE_CURRENT_SOURCE_DIR}/src/render/ir_raster_scoped_mixed_v5.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/render/ir_raster_image_clip_v1.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/render/ir_raster_image_v1.c")
target_compile_features(quantapdf_native_raster_objects PRIVATE c_std_11)
target_include_directories(quantapdf_native_raster_objects PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(quantapdf_native_raster_objects PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(quantapdf_native_raster_objects PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_native_raster_objects PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(quantapdf_backend STATIC)
add_subdirectory(src/backend)
target_compile_features(quantapdf_backend PRIVATE c_std_11 cxx_std_20)
target_include_directories(quantapdf_backend PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_backend PRIVATE
  $<BUILD_INTERFACE:QuantaPDF::PDFium>
  $<INSTALL_INTERFACE:QuantaPDF::_PDFium>
  qpdf::libqpdf
  JPEG::JPEG
  ZLIB::ZLIB)
if(WIN32)
  target_link_libraries(quantapdf_backend PRIVATE bcrypt)
elseif(APPLE)
  target_link_libraries(quantapdf_backend PRIVATE "-framework Security")
endif()
set_target_properties(quantapdf_backend PROPERTIES
  EXPORT_NAME _backend
  POSITION_INDEPENDENT_CODE ON)

add_library(quantapdf)
# Include the exact same qualified raster objects in the product target.
# Existing provider-backed rendering remains unchanged.
target_sources(quantapdf PRIVATE
  $<TARGET_OBJECTS:quantapdf_native_raster_objects>)
add_subdirectory(src/core)
add_subdirectory(src/document)
add_subdirectory(src/snapshot)
add_subdirectory(src/composer)
add_subdirectory(src/ir)
add_subdirectory(src/resource)
add_subdirectory(src/validation)
add_subdirectory(src/graph)
add_subdirectory(src/render)
add_library(QuantaPDF::QuantaPDF ALIAS quantapdf)
target_compile_features(quantapdf
  PUBLIC c_std_11
  PRIVATE cxx_std_20)
target_include_directories(quantapdf PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(quantapdf PRIVATE
  quantapdf_backend)
set_target_properties(quantapdf PROPERTIES
  EXPORT_NAME QuantaPDF
  LINKER_LANGUAGE CXX
  VERSION "${PROJECT_VERSION}"
  SOVERSION "${PROJECT_VERSION_MAJOR}")
quantapdf_configure_shared_exports(
  quantapdf "${CMAKE_CURRENT_SOURCE_DIR}/abi/quantapdf-v2.exports")

if(BUILD_SHARED_LIBS)
  target_compile_definitions(quantapdf
    PRIVATE QUANTAPDF_BUILDING_LIBRARY
    PUBLIC QUANTAPDF_SHARED)
endif()

if(MSVC)
  target_compile_options(quantapdf PRIVATE /W4 /WX)
  target_compile_options(quantapdf_backend PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(quantapdf_backend PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()

install(TARGETS quantapdf EXPORT QuantaPDFTargets
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/quantapdf"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

# A static SDK must carry the private archive and describe its link closure.
# Shared SDK consumers only link QuantaPDF; its backend is already linked in.
if(NOT BUILD_SHARED_LIBS)
  install(TARGETS quantapdf_backend EXPORT QuantaPDFTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()

if(WIN32)
  set(QUANTAPDF_PDFIUM_INSTALL_DIR "${CMAKE_INSTALL_BINDIR}")
else()
  set(QUANTAPDF_PDFIUM_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}")
endif()
install(FILES "$<TARGET_FILE:quantapdf_pdfium>"
  DESTINATION "${QUANTAPDF_PDFIUM_INSTALL_DIR}")
if(WIN32 AND NOT BUILD_SHARED_LIBS)
  install(FILES "$<TARGET_LINKER_FILE:quantapdf_pdfium>"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()

if(APPLE)
  set_property(TARGET quantapdf PROPERTY INSTALL_RPATH "@loader_path")
elseif(UNIX)
  set_property(TARGET quantapdf PROPERTY INSTALL_RPATH "$ORIGIN")
endif()

get_target_property(_quantapdf_pdfium_runtime quantapdf_pdfium IMPORTED_LOCATION)
get_filename_component(QUANTAPDF_PDFIUM_RUNTIME_NAME
  "${_quantapdf_pdfium_runtime}" NAME)
if(WIN32)
  get_target_property(_quantapdf_pdfium_import quantapdf_pdfium IMPORTED_IMPLIB)
  get_filename_component(QUANTAPDF_PDFIUM_IMPORT_NAME
    "${_quantapdf_pdfium_import}" NAME)
endif()
set(_quantapdf_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/QuantaPDF")
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuantaPDFConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/QuantaPDFConfig.cmake"
  INSTALL_DESTINATION "${_quantapdf_package_dir}"
  PATH_VARS QUANTAPDF_PDFIUM_INSTALL_DIR CMAKE_INSTALL_LIBDIR)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/QuantaPDFConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion)
install(EXPORT QuantaPDFTargets
  FILE QuantaPDFTargets.cmake
  NAMESPACE QuantaPDF::
  DESTINATION "${_quantapdf_package_dir}")
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/QuantaPDFConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/QuantaPDFConfigVersion.cmake"
  DESTINATION "${_quantapdf_package_dir}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/native-sdk.md"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/quantapdf/docs")
install(FILES
  "${QUANTAPDF_PDFIUM_ROOT}/LICENSE"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/quantapdf/licenses/pdfium")
install(DIRECTORY "${QUANTAPDF_PDFIUM_LICENSE_DIR}/"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/quantapdf/licenses/pdfium/licenses")
install(FILES "${QUANTAPDF_QPDF_COPYRIGHT}"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/quantapdf/licenses/qpdf")
install(FILES "${QUANTAPDF_LIBJPEG_TURBO_COPYRIGHT}"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/quantapdf/licenses/libjpeg-turbo")
install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
  "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY.md"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/quantapdf")

if(QUANTAPDF_BUILD_TESTS)
  target_compile_definitions(quantapdf PRIVATE QUANTAPDF_TESTING=1)
  include(CTest)
  add_subdirectory(tests)
  if(WIN32 AND BUILD_SHARED_LIBS)
    add_custom_command(TARGET quantapdf POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:quantapdf>
        "${CMAKE_CURRENT_BINARY_DIR}/tests"
      VERBATIM)
  endif()
  target_sources(quantapdf_test_pdf_form_mutation PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_form_rollback.c")
  set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_trim.c"
    TARGET_DIRECTORY quantapdf_test_pdf_trim
    PROPERTY COMPILE_DEFINITIONS "main=quantapdf_pdf_trim_base_main")
  target_sources(quantapdf_test_pdf_trim PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/qpdf_test_helpers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_trim_transforms.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_trim_policy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_trim_batch.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_trim_outside_crop.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_trim_main.c")
  target_link_libraries(quantapdf_test_pdf_trim PRIVATE qpdf::libqpdf)
  target_compile_features(quantapdf_test_pdf_trim PRIVATE cxx_std_20)
  set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_poster_split.c"
    TARGET_DIRECTORY quantapdf_test_pdf_poster_split
    PROPERTY COMPILE_DEFINITIONS "main=quantapdf_pdf_poster_split_base_main")
  target_sources(quantapdf_test_pdf_poster_split PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/qpdf_test_helpers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_poster_split_geometry.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_poster_split_policy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_poster_split_interactive.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_poster_split_navigation.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_poster_split_batch.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_poster_split_main.c")
  target_link_libraries(quantapdf_test_pdf_poster_split PRIVATE qpdf::libqpdf)
  target_compile_features(quantapdf_test_pdf_poster_split PRIVATE cxx_std_20)
  target_compile_definitions(quantapdf_test_pdf_poster_split PRIVATE
    POSTER_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/poster-split-output.pdf"
    POSTER_ROTATE_90_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-rotate-90.pdf"
    POSTER_USERUNIT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-userunit.pdf"
    POSTER_INTERACTIVE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-interactive.pdf"
    POSTER_NAVIGATION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-navigation.pdf"
    POSTER_NAVIGATION_ROTATE_90_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-navigation-rotate-90.pdf"
    POSTER_NAVIGATION_USERUNIT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-navigation-userunit.pdf"
    POSTER_MALFORMED_DESTINATION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-malformed-destination.pdf"
    POSTER_NONXYZ_DESTINATION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-nonxyz-destination.pdf"
    POSTER_NULLXYZ_DESTINATION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-nullxyz-destination.pdf"
    POSTER_OUTSIDE_DESTINATION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-outside-destination.pdf"
    POSTER_OUTLINE_UNSUPPORTED_ACTION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-outline-unsupported-action.pdf"
    POSTER_UNSELECTED_ANNOT_ACTION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-unselected-annot-action.pdf"
    POSTER_FIELD_ACTION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-field-action.pdf"
    POSTER_PRODUCTION_BOX_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-production-box.pdf"
    POSTER_TAGGED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-tagged.pdf"
    POSTER_ROOT_POLICY_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-root-policy.pdf"
    POSTER_UNSELECTED_ACTIONS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-unselected-actions.pdf"
    POSTER_FORM_ACTIONS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-form-actions.pdf"
    POSTER_MALFORMED_ANNOTS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-malformed-annots.pdf"
    POSTER_CROSSING_ANNOTATION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-crossing-annotation.pdf"
    POSTER_CROSSING_WIDGET_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-crossing-widget.pdf"
    POSTER_ORPHAN_WIDGET_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-orphan-widget.pdf")
  target_compile_definitions(quantapdf_test_pdf_poster_split PRIVATE
    POSTER_PERMS_SIGNATURE_PDF="${CMAKE_CURRENT_BINARY_DIR}/poster-perms-signature.pdf")
endif()
