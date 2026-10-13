# Selected Graph /Resources/ExtGState admission, partial opacity deltas,
# q/Q, Clip, blend and source-vs-export PDFium pixel conformance.
add_executable(quantapdf_test_pdf_extgstate test_pdf_extgstate.cpp
  ../src/graph/graph_extgstate.c ../src/graph/graph_page_geometry.c
  ../src/document/page_composer_ir.c ../src/document/page_content_ir.c
  ../src/document/document_graph.c
  ../src/ir/pdf_path_lowering.c ../src/ir/pdf_graphics_state.c
  ../src/ir/pdf_content_ir.c ../src/ir/ir.c
  ../src/resource/resource_registry.c
  ../src/core/context_lifetime.c ../src/core/output_lifetime.c
  ../src/snapshot/snapshot_owner.c)
target_include_directories(quantapdf_test_pdf_extgstate PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_pdf_extgstate PRIVATE
  QuantaPDF::QuantaPDF quantapdf_backend QuantaPDF::PDFium qpdf::libqpdf)
target_compile_features(quantapdf_test_pdf_extgstate PRIVATE c_std_11 cxx_std_20)
target_compile_definitions(quantapdf_test_pdf_extgstate PRIVATE
  EXTGSTATE_SOURCE_PDF="${CMAKE_CURRENT_BINARY_DIR}/extgstate-source.pdf"
  EXTGSTATE_LOWERED_PDF="${CMAKE_CURRENT_BINARY_DIR}/extgstate-lowered.pdf")
if(MSVC)
  target_compile_options(quantapdf_test_pdf_extgstate PRIVATE /utf-8)
endif()
add_test(NAME quantapdf.pdf_extgstate COMMAND quantapdf_test_pdf_extgstate)
set_tests_properties(quantapdf.pdf_extgstate PROPERTIES TIMEOUT 30)

# Pure C11 clip-chain lowering: W/W* delayed activation, nested q/Q,
# ownership, cardinality and aggregate command admission budgets.
add_executable(quantapdf_test_pdf_clip_lowering test_pdf_clip_lowering.c
  ../src/ir/pdf_content_ir.c ../src/ir/pdf_graphics_state.c
  ../src/ir/pdf_path_lowering.c ../src/ir/ir.c
  ../src/resource/resource_registry.c)
target_include_directories(quantapdf_test_pdf_clip_lowering PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_pdf_clip_lowering PRIVATE c_std_11)
if(UNIX)
  target_link_libraries(quantapdf_test_pdf_clip_lowering PRIVATE m)
endif()
add_test(NAME quantapdf.pdf_clip_lowering COMMAND quantapdf_test_pdf_clip_lowering)
set_tests_properties(quantapdf.pdf_clip_lowering PROPERTIES TIMEOUT 30)

# Real QPDF source vs Composer re-export PDFium raster comparison.
add_executable(quantapdf_test_pdf_clip_pixels test_pdf_clip_pixels.cpp
  ../src/graph/graph_page_geometry.c
  ../src/document/page_composer_ir.c ../src/document/page_content_ir.c
  ../src/document/document_graph.c
  ../src/ir/pdf_path_lowering.c ../src/ir/pdf_graphics_state.c
  ../src/ir/pdf_content_ir.c ../src/ir/ir.c
  ../src/resource/resource_registry.c
  ../src/core/context_lifetime.c ../src/core/output_lifetime.c
  ../src/snapshot/snapshot_owner.c)
target_include_directories(quantapdf_test_pdf_clip_pixels PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_pdf_clip_pixels PRIVATE
  QuantaPDF::QuantaPDF quantapdf_backend QuantaPDF::PDFium qpdf::libqpdf)
target_compile_features(quantapdf_test_pdf_clip_pixels PRIVATE c_std_11 cxx_std_20)
target_compile_definitions(quantapdf_test_pdf_clip_pixels PRIVATE
  CLIP_SOURCE_PDF="${CMAKE_CURRENT_BINARY_DIR}/clip-original.pdf"
  CLIP_LOWERED_PDF="${CMAKE_CURRENT_BINARY_DIR}/clip-export.pdf")
if(MSVC)
  target_compile_options(quantapdf_test_pdf_clip_pixels PRIVATE /utf-8)
endif()
add_test(NAME quantapdf.pdf_clip_pixels COMMAND quantapdf_test_pdf_clip_pixels)
set_tests_properties(quantapdf.pdf_clip_pixels PROPERTIES TIMEOUT 30)

# Private page display affine map, exact Composer lowering, and source-vs-output pixels.
add_executable(quantapdf_test_pdf_page_display test_pdf_page_display.cpp
  ../src/graph/graph_page_geometry.c
  ../src/document/page_composer_ir.c ../src/document/page_content_ir.c
  ../src/document/document_graph.c
  ../src/ir/pdf_path_lowering.c ../src/ir/pdf_graphics_state.c
  ../src/ir/pdf_content_ir.c ../src/ir/ir.c
  ../src/resource/resource_registry.c
  ../src/core/context_lifetime.c ../src/core/output_lifetime.c
  ../src/snapshot/snapshot_owner.c)
target_include_directories(quantapdf_test_pdf_page_display PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_pdf_page_display PRIVATE
  QuantaPDF::QuantaPDF quantapdf_backend QuantaPDF::PDFium qpdf::libqpdf)
target_compile_features(quantapdf_test_pdf_page_display PRIVATE c_std_11 cxx_std_20)
target_compile_definitions(quantapdf_test_pdf_page_display PRIVATE
  DISPLAY_SOURCE_PDF="${CMAKE_CURRENT_BINARY_DIR}/display-source.pdf"
  DISPLAY_RESULT_PDF="${CMAKE_CURRENT_BINARY_DIR}/display-result.pdf")
if(MSVC)
  target_compile_options(quantapdf_test_pdf_page_display PRIVATE /utf-8)
endif()
add_test(NAME quantapdf.pdf_page_display COMMAND quantapdf_test_pdf_page_display)
set_tests_properties(quantapdf.pdf_page_display PROPERTIES TIMEOUT 30)

# Versioned graph page-geometry and Snapshot->Composer IR admission regression.
add_executable(quantapdf_test_graph_page_geometry test_graph_page_geometry.cpp
  ../src/graph/graph_page_geometry.c
  ../src/document/page_composer_ir.c ../src/document/page_content_ir.c
  ../src/document/document_graph.c
  ../src/ir/pdf_path_lowering.c ../src/ir/pdf_graphics_state.c
  ../src/ir/pdf_content_ir.c ../src/ir/ir.c
  ../src/resource/resource_registry.c
  ../src/core/context_lifetime.c ../src/core/output_lifetime.c
  ../src/snapshot/snapshot_owner.c)
target_include_directories(quantapdf_test_graph_page_geometry PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_graph_page_geometry PRIVATE
  QuantaPDF::QuantaPDF quantapdf_backend QuantaPDF::PDFium qpdf::libqpdf)
target_compile_features(quantapdf_test_graph_page_geometry PRIVATE c_std_11 cxx_std_20)
target_compile_definitions(quantapdf_test_graph_page_geometry PRIVATE
  GEOMETRY_SOURCE_PDF="${CMAKE_CURRENT_BINARY_DIR}/geometry-original.pdf"
  GEOMETRY_RESULT_PDF="${CMAKE_CURRENT_BINARY_DIR}/geometry-lowered.pdf")
if(MSVC)
  target_compile_options(quantapdf_test_graph_page_geometry PRIVATE /utf-8)
endif()
add_test(NAME quantapdf.graph_page_geometry COMMAND quantapdf_test_graph_page_geometry)
set_tests_properties(quantapdf.graph_page_geometry PROPERTIES TIMEOUT 30)

# Private PDF path->Composer IR gate with real QPDF export/PDFium render.
add_executable(quantapdf_test_pdf_path_lowering test_pdf_path_lowering.c
  ../src/ir/pdf_content_ir.c ../src/ir/pdf_graphics_state.c
  ../src/ir/pdf_path_lowering.c ../src/ir/ir.c
  ../src/resource/resource_registry.c)
target_include_directories(quantapdf_test_pdf_path_lowering PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_pdf_path_lowering PRIVATE
  QuantaPDF::QuantaPDF quantapdf_backend QuantaPDF::PDFium)
target_compile_features(quantapdf_test_pdf_path_lowering PRIVATE c_std_11)
if(UNIX)
  target_link_libraries(quantapdf_test_pdf_path_lowering PRIVATE m)
endif()
target_compile_definitions(quantapdf_test_pdf_path_lowering PRIVATE
  PATH_LOWER_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/pdf-path-lowered.pdf")
add_test(NAME quantapdf.pdf_path_lowering COMMAND quantapdf_test_pdf_path_lowering)
set_tests_properties(quantapdf.pdf_path_lowering PROPERTIES TIMEOUT 30)

# Pure C11 PDF graphics semantics; no provider, PDFium, or QPDF.
add_executable(quantapdf_test_pdf_graphics_state test_pdf_graphics_state.c
  ../src/ir/pdf_content_ir.c ../src/ir/pdf_graphics_state.c)
target_include_directories(quantapdf_test_pdf_graphics_state PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_pdf_graphics_state PRIVATE c_std_11)
if(UNIX)
  target_link_libraries(quantapdf_test_pdf_graphics_state PRIVATE m)
endif()
add_test(NAME quantapdf.pdf_graphics_state COMMAND quantapdf_test_pdf_graphics_state)
set_tests_properties(quantapdf.pdf_graphics_state PROPERTIES TIMEOUT 30)

# Provider-independent pure C11 parser; no PDFium or QPDF in this test.
add_executable(quantapdf_test_pdf_content_ir test_pdf_content_ir.c ../src/ir/pdf_content_ir.c)
target_include_directories(quantapdf_test_pdf_content_ir PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_pdf_content_ir PRIVATE c_std_11)
if(UNIX)
  target_link_libraries(quantapdf_test_pdf_content_ir PRIVATE m)
endif()
add_test(NAME quantapdf.pdf_content_ir COMMAND quantapdf_test_pdf_content_ir)
set_tests_properties(quantapdf.pdf_content_ir PROPERTIES TIMEOUT 30)

# Pure render planning has no SDK/provider dependency.
add_executable(quantapdf_test_render_plan test_render_plan.c ../src/render/render_plan.c)
target_include_directories(quantapdf_test_render_plan PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_render_plan PRIVATE c_std_11)
if(UNIX)
  target_link_libraries(quantapdf_test_render_plan PRIVATE m)
endif()
add_test(NAME quantapdf.render_plan COMMAND quantapdf_test_render_plan)
set_tests_properties(quantapdf.render_plan PROPERTIES TIMEOUT 30)

# Pure model/validator tests do not link the SDK or any parser.
add_executable(quantapdf_test_graph test_graph.c
  ../src/graph/graph.c ../src/graph/identity.c ../src/validation/graph_validation.c)
target_include_directories(quantapdf_test_graph PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_graph PRIVATE c_std_11)
add_test(NAME quantapdf.graph COMMAND quantapdf_test_graph)
set_tests_properties(quantapdf.graph PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_graph_edit test_graph_edit.c
  ../src/graph/graph.c ../src/graph/graph_query.c ../src/graph/identity.c ../src/validation/graph_validation.c)
target_include_directories(quantapdf_test_graph_edit PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_graph_edit PRIVATE c_std_11)
add_test(NAME quantapdf.graph_edit COMMAND quantapdf_test_graph_edit)
set_tests_properties(quantapdf.graph_edit PROPERTIES TIMEOUT 30)

# Compile the actual private facade/lifetime helpers without adding SDK exports.
add_executable(quantapdf_test_graph_snapshot test_graph_snapshot.c
  ../src/document/document_graph.c ../src/document/page_content_ir.c
  ../src/ir/pdf_content_ir.c ../src/ir/pdf_graphics_state.c
  ../src/core/context_lifetime.c ../src/core/output_lifetime.c ../src/snapshot/snapshot_owner.c)
target_include_directories(quantapdf_test_graph_snapshot PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_graph_snapshot PRIVATE
  QuantaPDF::QuantaPDF quantapdf_backend QuantaPDF::PDFium)
target_compile_definitions(quantapdf_test_graph_snapshot PRIVATE
  QUANTAPDF_TESTING=1
  GRAPH_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  GRAPH_METADATA_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/metadata-info.pdf"
  GRAPH_FORM_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-main.pdf"
  GRAPH_OUTLINE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/outline-tree.pdf"
  GRAPH_ENCRYPTED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/encrypted-one-page.pdf"
  GRAPH_IMAGES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-images.pdf")
add_test(NAME quantapdf.graph_snapshot COMMAND quantapdf_test_graph_snapshot)
set_tests_properties(quantapdf.graph_snapshot PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_graph_import test_graph_import.cpp)
target_include_directories(quantapdf_test_graph_import PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_graph_import PRIVATE
  QuantaPDF::QuantaPDF quantapdf_backend QuantaPDF::PDFium qpdf::libqpdf)
target_compile_features(quantapdf_test_graph_import PRIVATE cxx_std_20)
if(MSVC)
  # QPDFPageObjectHelper includes UTF-8 diagram comments in QPDFMatrix.hh.
  target_compile_options(quantapdf_test_graph_import PRIVATE /utf-8)
endif()
add_test(NAME quantapdf.graph_import COMMAND quantapdf_test_graph_import)
set_tests_properties(quantapdf.graph_import PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_graph_write test_graph_write.cpp)
target_include_directories(quantapdf_test_graph_write PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_graph_write PRIVATE
  QuantaPDF::QuantaPDF quantapdf_backend QuantaPDF::PDFium qpdf::libqpdf)
target_compile_definitions(quantapdf_test_graph_write PRIVATE
  GRAPH_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  GRAPH_ENCRYPTED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/encrypted-one-page.pdf")
target_compile_features(quantapdf_test_graph_write PRIVATE cxx_std_20)
add_test(NAME quantapdf.graph_write COMMAND quantapdf_test_graph_write)
set_tests_properties(quantapdf.graph_write PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_graph_stream test_graph_stream.cpp)
target_include_directories(quantapdf_test_graph_stream PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_graph_stream PRIVATE quantapdf_backend qpdf::libqpdf ZLIB::ZLIB)
target_compile_features(quantapdf_test_graph_stream PRIVATE cxx_std_20)
add_test(NAME quantapdf.graph_stream COMMAND quantapdf_test_graph_stream)
set_tests_properties(quantapdf.graph_stream PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_pdf_text test_pdf_text.cpp)
target_include_directories(quantapdf_test_pdf_text PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_pdf_text PRIVATE quantapdf_backend qpdf::libqpdf)
target_compile_features(quantapdf_test_pdf_text PRIVATE cxx_std_20)
add_test(NAME quantapdf.pdf_text COMMAND quantapdf_test_pdf_text)
set_tests_properties(quantapdf.pdf_text PROPERTIES TIMEOUT 30)

# Compile the identity contract without any backend or SDK library dependency.
add_executable(quantapdf_test_identity
  test_identity.c ../src/graph/identity.c)
target_include_directories(quantapdf_test_identity PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_identity PRIVATE c_std_11)
add_test(NAME quantapdf.identity COMMAND quantapdf_test_identity)
set_tests_properties(quantapdf.identity PROPERTIES TIMEOUT 30)

# Compile and link rules without any backend or SDK library dependency.
add_executable(quantapdf_test_validation
  test_validation.c ../src/validation/validation.c)
target_include_directories(quantapdf_test_validation PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_validation PRIVATE c_std_11)
add_test(NAME quantapdf.validation COMMAND quantapdf_test_validation)
set_tests_properties(quantapdf.validation PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_resource_registry
  test_resource_registry.c ../src/resource/resource_registry.c)
target_include_directories(quantapdf_test_resource_registry PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_resource_registry PRIVATE QuantaPDF::QuantaPDF)
add_test(NAME quantapdf.resource_registry COMMAND quantapdf_test_resource_registry)
set_tests_properties(quantapdf.resource_registry PROPERTIES TIMEOUT 30)

# Compile the private owner into this boundary test without exporting it in the SDK.
add_executable(quantapdf_test_ir test_ir.c ../src/ir/ir.c
  ../src/resource/resource_registry.c)
target_include_directories(quantapdf_test_ir PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_ir PRIVATE
  QuantaPDF::QuantaPDF quantapdf_backend QuantaPDF::PDFium)
target_compile_definitions(quantapdf_test_ir PRIVATE
  QUANTAPDF_TESTING=1
  IR_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/ir-detached.pdf")
add_test(NAME quantapdf.ir COMMAND quantapdf_test_ir)
set_tests_properties(quantapdf.ir PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_context test_context.c
  ../src/snapshot/snapshot_owner.c)
target_include_directories(quantapdf_test_context PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_context PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_context PRIVATE
  QUANTAPDF_TESTING=1
  TEXT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/text-one-page.pdf"
  IMAGES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-images.pdf"
  NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt")
add_test(NAME quantapdf.context COMMAND quantapdf_test_context)
set_tests_properties(quantapdf.context PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_backend_foundation
  test_backend_foundation.c
  backend_dispatch_test.c)
target_include_directories(quantapdf_test_backend_foundation PRIVATE
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_backend_foundation PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  QuantaPDF::PDFium)
target_compile_definitions(quantapdf_test_backend_foundation PRIVATE
  QUANTAPDF_TESTING=1
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

add_executable(quantapdf_test_composer_clip_intersection
  test_composer_clip_intersection.c
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_clip_intersection PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_clip_intersection PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_clip_intersection PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_clip_intersection PRIVATE
  COMPOSER_COMPOUND_CLIP_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-compound-clip-output.pdf")
add_test(NAME quantapdf.composer_clip_intersection
  COMMAND quantapdf_test_composer_clip_intersection)
set_tests_properties(quantapdf.composer_clip_intersection PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_form_xobject
  test_composer_form_xobject.c
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_form_xobject PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_form_xobject PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_form_xobject PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_form_xobject PRIVATE
  COMPOSER_FORM_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-form-output.pdf")
add_test(NAME quantapdf.composer_form_xobject
  COMMAND quantapdf_test_composer_form_xobject)
set_tests_properties(quantapdf.composer_form_xobject PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_soft_mask
  test_composer_soft_mask.c
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_soft_mask PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_soft_mask PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_soft_mask PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_soft_mask PRIVATE
  COMPOSER_SOFT_MASK_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-soft-mask-output.pdf")
add_test(NAME quantapdf.composer_soft_mask
  COMMAND quantapdf_test_composer_soft_mask)
set_tests_properties(quantapdf.composer_soft_mask PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_form_group
  test_composer_form_group.c
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_form_group PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_form_group PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_form_group PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_form_group PRIVATE
  COMPOSER_FORM_GROUP_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-form-group-output.pdf")
add_test(NAME quantapdf.composer_form_group
  COMMAND quantapdf_test_composer_form_group)
set_tests_properties(quantapdf.composer_form_group PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_path_affine
  test_composer_path_affine.c
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_path_affine PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_path_affine PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_path_affine PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_path_affine PRIVATE
  COMPOSER_PATH_AFFINE_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-path-affine-output.pdf")
add_test(NAME quantapdf.composer_path_affine
  COMMAND quantapdf_test_composer_path_affine)
set_tests_properties(quantapdf.composer_path_affine PROPERTIES TIMEOUT 30)

add_executable(quantapdf_test_composer_tiling_pattern
  test_composer_tiling_pattern.c
  composer_test_helpers.cpp)
target_include_directories(quantapdf_test_composer_tiling_pattern PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_composer_tiling_pattern PRIVATE
  QuantaPDF::QuantaPDF
  quantapdf_backend
  qpdf::libqpdf
  ZLIB::ZLIB)
target_compile_features(quantapdf_test_composer_tiling_pattern PRIVATE cxx_std_20)
target_compile_definitions(quantapdf_test_composer_tiling_pattern PRIVATE
  COMPOSER_TILING_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composer-tiling-output.pdf")
add_test(NAME quantapdf.composer_tiling_pattern
  COMMAND quantapdf_test_composer_tiling_pattern)
set_tests_properties(quantapdf.composer_tiling_pattern PROPERTIES TIMEOUT 30)

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
  RENDER_PATTERN_PDF="${CMAKE_CURRENT_BINARY_DIR}/render-pattern.pdf"
  RENDER_CROPPED_PATTERN_PDF="${CMAKE_CURRENT_BINARY_DIR}/render-cropped-pattern.pdf"
  RENDER_INTRINSIC_ROTATION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/poster-rotate-90.pdf"
  RENDER_USERUNIT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/poster-userunit.pdf"
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
  NO_ANNOTS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  NONARRAY_ANNOTS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/annotations-nonarray.pdf"
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


# #173 product conformance: validate externally visible SDK contracts separately
# from backend-internal tests. Existing suites remain the authoritative semantic
# corpus; labels provide a stable cross-platform release qualification entrypoint.
add_executable(quantapdf_test_product_conformance test_product_conformance.c)
target_link_libraries(quantapdf_test_product_conformance PRIVATE
  QuantaPDF::QuantaPDF)
target_compile_features(quantapdf_test_product_conformance PRIVATE c_std_11)
if(MSVC)
  target_compile_options(quantapdf_test_product_conformance PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_test_product_conformance PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.product_conformance
  COMMAND quantapdf_test_product_conformance
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt")
set_tests_properties(quantapdf.product_conformance PROPERTIES TIMEOUT 30)

# Must opt into the same runtime loader search path as the existing CTests:
# the product test is registered after the earlier directory TESTS enumeration.
if(WIN32)
  set_property(TEST quantapdf.product_conformance PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.product_conformance PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.product_conformance PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

set(_qp_conformance_tests
  quantapdf.product_conformance
  quantapdf.version
  quantapdf.status
  quantapdf.context
  quantapdf.document
  quantapdf.render
  quantapdf.text
  quantapdf.structured_text
  quantapdf.pdf_rewrite_lossless
  quantapdf.pdf_security
  quantapdf.pdf_security_rewrite
  quantapdf.pdf_export
  quantapdf.pdf_image_recompression
  quantapdf.graph_snapshot
  quantapdf.graph_edit
  quantapdf.validation
  quantapdf.composer
  quantapdf.pdf_clip_pixels
  quantapdf.pdf_extgstate)
foreach(_qp_test IN LISTS _qp_conformance_tests)
  if(NOT TEST "${_qp_test}")
    message(FATAL_ERROR "Product conformance test missing: ${_qp_test}")
  endif()
  set_property(TEST "${_qp_test}" APPEND PROPERTY LABELS conformance)
endforeach()
# On shared SDK builds the checked export table is itself a conformance test.
if(TEST quantapdf.abi_exports)
  set_property(TEST quantapdf.abi_exports APPEND PROPERTY LABELS conformance)
endif()
unset(_qp_test)
unset(_qp_conformance_tests)

# Private native raster V0 qualification. Standalone pure C11: deliberately
# does NOT link QuantaPDF::QuantaPDF, qpdf::libqpdf, PDFium or backend objects.
add_executable(quantapdf_test_native_ir_raster
  test_ir_raster.c
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
target_include_directories(quantapdf_test_native_ir_raster PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_native_ir_raster PRIVATE c_std_11)
if(MSVC)
  target_compile_options(quantapdf_test_native_ir_raster PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_test_native_ir_raster PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_ir_raster
  COMMAND quantapdf_test_native_ir_raster)
set_tests_properties(quantapdf.native_ir_raster PROPERTIES
  TIMEOUT 15 LABELS "conformance;native-raster")

# Pure C11 general-path raster gate, no QuantaPDF/PDFium/qpdf linking.
add_executable(quantapdf_test_native_ir_raster_v1
  test_ir_raster_v1.c
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
target_include_directories(quantapdf_test_native_ir_raster_v1 PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_native_ir_raster_v1 PRIVATE c_std_11)
if(MSVC)
  target_compile_options(quantapdf_test_native_ir_raster_v1 PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_test_native_ir_raster_v1 PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
if(UNIX)
  target_link_libraries(quantapdf_test_native_ir_raster_v1 PRIVATE m)
endif()
add_test(NAME quantapdf.native_ir_raster_v1
  COMMAND quantapdf_test_native_ir_raster_v1)
set_tests_properties(quantapdf.native_ir_raster_v1 PROPERTIES
  TIMEOUT 30 LABELS "conformance;native-raster")


# Native IR vs PDFium differential oracle: PDFium and QPDF are test-only
# dependencies; the independent C11 native-raster suite above remains backend-free.
add_executable(quantapdf_test_native_ir_raster_oracle
  test_ir_raster_oracle.cpp
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
target_include_directories(quantapdf_test_native_ir_raster_oracle PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_native_ir_raster_oracle PRIVATE
  QuantaPDF::QuantaPDF quantapdf_backend QuantaPDF::PDFium qpdf::libqpdf)
target_compile_features(quantapdf_test_native_ir_raster_oracle PRIVATE
  c_std_11 cxx_std_20)
target_compile_definitions(quantapdf_test_native_ir_raster_oracle PRIVATE
  ORACLE_RECT_PDF="${CMAKE_CURRENT_BINARY_DIR}/native-oracle-rect.pdf"
  ORACLE_HOLE_PDF="${CMAKE_CURRENT_BINARY_DIR}/native-oracle-hole.pdf"
  ORACLE_TRIANGLE_PDF="${CMAKE_CURRENT_BINARY_DIR}/native-oracle-triangle.pdf"
  ORACLE_CUBIC_PDF="${CMAKE_CURRENT_BINARY_DIR}/native-oracle-cubic.pdf")
if(MSVC)
  target_compile_options(quantapdf_test_native_ir_raster_oracle PRIVATE /utf-8)
endif()
add_test(NAME quantapdf.native_ir_raster_oracle
  COMMAND quantapdf_test_native_ir_raster_oracle)
set_tests_properties(quantapdf.native_ir_raster_oracle PROPERTIES
  TIMEOUT 40 LABELS "conformance;native-raster")
# Registered after the shared CTest environment setup; apply the provider
# runtime loader directory explicitly, as in product_conformance.
if(WIN32)
  set_property(TEST quantapdf.native_ir_raster_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.native_ir_raster_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.native_ir_raster_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()


# Standalone C11 stroke qualification: no PDFium, qpdf or runtime linking.
add_executable(quantapdf_test_native_ir_stroke_butt_v0
  test_ir_stroke_v0.c
  "${PROJECT_SOURCE_DIR}/src/render/ir_stroke_v0.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
target_include_directories(quantapdf_test_native_ir_stroke_butt_v0 PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_native_ir_stroke_butt_v0 PRIVATE c_std_11)
if(MSVC)
  target_compile_options(quantapdf_test_native_ir_stroke_butt_v0 PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_test_native_ir_stroke_butt_v0 PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
if(UNIX)
  target_link_libraries(quantapdf_test_native_ir_stroke_butt_v0 PRIVATE m)
endif()
add_test(NAME quantapdf.native_ir_stroke_butt_v0
  COMMAND quantapdf_test_native_ir_stroke_butt_v0)
set_tests_properties(quantapdf.native_ir_stroke_butt_v0 PROPERTIES
  TIMEOUT 30 LABELS "conformance;native-raster")


# Standalone native Clip C11 test, no PDFium/qpdf or SDK runtime linkage.
add_executable(quantapdf_test_native_ir_clip_rect_v0
  test_ir_clip_v0.c
  "${PROJECT_SOURCE_DIR}/src/render/ir_clip_v0.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
target_include_directories(quantapdf_test_native_ir_clip_rect_v0 PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_native_ir_clip_rect_v0 PRIVATE c_std_11)
if(MSVC)
  target_compile_options(quantapdf_test_native_ir_clip_rect_v0 PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_test_native_ir_clip_rect_v0 PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
if(UNIX)
  target_link_libraries(quantapdf_test_native_ir_clip_rect_v0 PRIVATE m)
endif()
add_test(NAME quantapdf.native_ir_clip_rect_v0
  COMMAND quantapdf_test_native_ir_clip_rect_v0)
set_tests_properties(quantapdf.native_ir_clip_rect_v0 PROPERTIES
  TIMEOUT 30 LABELS "conformance;native-raster")


# Flat intersection admission is independently compiled without PDFium/qpdf.
add_executable(quantapdf_test_native_clip_intersection_v1
  test_ir_clip_intersection_v1.c
  "${PROJECT_SOURCE_DIR}/src/render/ir_clip_intersection_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_clip_v0.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
target_include_directories(quantapdf_test_native_clip_intersection_v1 PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_native_clip_intersection_v1 PRIVATE c_std_11)
if(MSVC)
  target_compile_options(quantapdf_test_native_clip_intersection_v1 PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_test_native_clip_intersection_v1 PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
if(UNIX)
  target_link_libraries(quantapdf_test_native_clip_intersection_v1 PRIVATE m)
endif()
add_test(NAME quantapdf.native_clip_intersection_v1
  COMMAND quantapdf_test_native_clip_intersection_v1)
set_tests_properties(quantapdf.native_clip_intersection_v1 PROPERTIES
  TIMEOUT 30 LABELS "conformance;native-raster")


# Independent PDFium differential oracle for Native Clip resource semantics.
# This target alone links PDFium/qpdf; pure C11 production/native tests do not.
add_executable(quantapdf_test_native_clip_intersection_oracle
  test_ir_clip_intersection_oracle.cpp
  "${PROJECT_SOURCE_DIR}/src/render/ir_clip_intersection_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_clip_v0.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
target_include_directories(quantapdf_test_native_clip_intersection_oracle PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_native_clip_intersection_oracle PRIVATE
  QuantaPDF::QuantaPDF quantapdf_backend QuantaPDF::PDFium qpdf::libqpdf)
target_compile_features(quantapdf_test_native_clip_intersection_oracle PRIVATE
  c_std_11 cxx_std_20)
target_compile_definitions(quantapdf_test_native_clip_intersection_oracle PRIVATE
  ORACLE_TWO_PDF="${CMAKE_CURRENT_BINARY_DIR}/native-clip-oracle-two.pdf"
  ORACLE_THREE_PDF="${CMAKE_CURRENT_BINARY_DIR}/native-clip-oracle-three.pdf")
if(MSVC)
  target_compile_options(quantapdf_test_native_clip_intersection_oracle
    PRIVATE /utf-8)
endif()
add_test(NAME quantapdf.native_clip_intersection_oracle
  COMMAND quantapdf_test_native_clip_intersection_oracle)
set_tests_properties(quantapdf.native_clip_intersection_oracle PROPERTIES
  TIMEOUT 40 LABELS "conformance;native-raster")
# Added after common test environment setup; configure the runtime PDFium path.
if(WIN32)
  set_property(TEST quantapdf.native_clip_intersection_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.native_clip_intersection_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.native_clip_intersection_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()


# Pure C11 per-sample polygon Clip conformance: no PDFium/qpdf/SDK links.
add_executable(quantapdf_test_native_clip_path_v2
  test_ir_clip_path_v2.c
  "${PROJECT_SOURCE_DIR}/src/render/ir_clip_path_v2.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
target_include_directories(quantapdf_test_native_clip_path_v2 PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_native_clip_path_v2 PRIVATE c_std_11)
if(MSVC)
  target_compile_options(quantapdf_test_native_clip_path_v2 PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_test_native_clip_path_v2 PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
if(UNIX)
  target_link_libraries(quantapdf_test_native_clip_path_v2 PRIVATE m)
endif()
add_test(NAME quantapdf.native_clip_path_v2
  COMMAND quantapdf_test_native_clip_path_v2)
set_tests_properties(quantapdf.native_clip_path_v2 PROPERTIES
  TIMEOUT 30 LABELS "conformance;native-raster")


# Differential oracle only: this target links PDFium/qpdf for reference pixels.
# The pure C11 native_polygon Clip tests/production sources remain engine-free.
add_executable(quantapdf_test_native_polygon_clip_oracle
  test_ir_polygon_clip_oracle.cpp
  "${PROJECT_SOURCE_DIR}/src/render/ir_clip_path_v2.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
target_include_directories(quantapdf_test_native_polygon_clip_oracle PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_native_polygon_clip_oracle PRIVATE
  QuantaPDF::QuantaPDF quantapdf_backend QuantaPDF::PDFium qpdf::libqpdf)
target_compile_features(quantapdf_test_native_polygon_clip_oracle PRIVATE
  c_std_11 cxx_std_20)
target_compile_definitions(quantapdf_test_native_polygon_clip_oracle PRIVATE
  ORACLE_TRIANGLE_PDF="${CMAKE_CURRENT_BINARY_DIR}/polygon-clip-triangle.pdf"
  ORACLE_HOLE_PDF="${CMAKE_CURRENT_BINARY_DIR}/polygon-clip-hole.pdf"
  ORACLE_FRACTIONAL_PDF="${CMAKE_CURRENT_BINARY_DIR}/polygon-clip-fraction.pdf"
  ORACLE_BEZIER_PDF="${CMAKE_CURRENT_BINARY_DIR}/polygon-clip-bezier.pdf")
if(MSVC)
  target_compile_options(quantapdf_test_native_polygon_clip_oracle PRIVATE /utf-8)
endif()
add_test(NAME quantapdf.native_polygon_clip_oracle
  COMMAND quantapdf_test_native_polygon_clip_oracle)
set_tests_properties(quantapdf.native_polygon_clip_oracle PROPERTIES
  TIMEOUT 40 LABELS "conformance;native-raster")
if(WIN32)
  set_property(TEST quantapdf.native_polygon_clip_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.native_polygon_clip_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.native_polygon_clip_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()


# Native C11 scoped draw sequence: standalone, no PDFium/qpdf/runtime target.
add_executable(quantapdf_test_native_scoped_operations_v0
  test_ir_scoped_operations_v0.c
  "${PROJECT_SOURCE_DIR}/src/render/ir_scoped_operations_v0.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
target_include_directories(quantapdf_test_native_scoped_operations_v0 PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_native_scoped_operations_v0 PRIVATE c_std_11)
if(MSVC)
  target_compile_options(quantapdf_test_native_scoped_operations_v0 PRIVATE
    /W4 /WX)
else()
  target_compile_options(quantapdf_test_native_scoped_operations_v0 PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
if(UNIX)
  target_link_libraries(quantapdf_test_native_scoped_operations_v0 PRIVATE m)
endif()
add_test(NAME quantapdf.native_scoped_operations_v0
  COMMAND quantapdf_test_native_scoped_operations_v0)
set_tests_properties(quantapdf.native_scoped_operations_v0 PROPERTIES
  TIMEOUT 30 LABELS "conformance;native-raster")


# Differential oracle ONLY: PDFium/qpdf are linked here, never in native C11.
add_executable(quantapdf_test_native_scoped_operations_oracle
  test_ir_scoped_operations_oracle.cpp
  "${PROJECT_SOURCE_DIR}/src/render/ir_scoped_operations_v0.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
target_include_directories(quantapdf_test_native_scoped_operations_oracle PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_native_scoped_operations_oracle PRIVATE
  QuantaPDF::QuantaPDF quantapdf_backend QuantaPDF::PDFium qpdf::libqpdf)
target_compile_features(quantapdf_test_native_scoped_operations_oracle PRIVATE
  c_std_11 cxx_std_20)
target_compile_definitions(quantapdf_test_native_scoped_operations_oracle PRIVATE
  ORACLE_SCOPES_PDF="${CMAKE_CURRENT_BINARY_DIR}/scoped-oracle-scopes.pdf"
  ORACLE_NO_LEAK_PDF="${CMAKE_CURRENT_BINARY_DIR}/scoped-oracle-no-leak.pdf"
  ORACLE_REVERSED_PDF="${CMAKE_CURRENT_BINARY_DIR}/scoped-oracle-reverse.pdf"
  ORACLE_FRACTIONAL_PDF="${CMAKE_CURRENT_BINARY_DIR}/scoped-oracle-fraction.pdf")
if(MSVC)
  target_compile_options(quantapdf_test_native_scoped_operations_oracle
    PRIVATE /utf-8)
endif()
add_test(NAME quantapdf.native_scoped_operations_oracle
  COMMAND quantapdf_test_native_scoped_operations_oracle)
set_tests_properties(quantapdf.native_scoped_operations_oracle PROPERTIES
  TIMEOUT 40 LABELS "conformance;native-raster")
if(WIN32)
  set_property(TEST quantapdf.native_scoped_operations_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.native_scoped_operations_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.native_scoped_operations_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()


# Standalone nested Clip DAG runtime. This pure C11 test intentionally links
# neither QuantaPDF nor qpdf/PDFium; pixel oracles remain test-only and separate.
add_executable(quantapdf_test_native_clip_dag_v3
  test_ir_clip_dag_v3.c
  "${PROJECT_SOURCE_DIR}/src/render/ir_clip_dag_v3.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
target_include_directories(quantapdf_test_native_clip_dag_v3 PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_native_clip_dag_v3 PRIVATE c_std_11)
if(MSVC)
  target_compile_options(quantapdf_test_native_clip_dag_v3 PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_test_native_clip_dag_v3 PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
if(UNIX)
  target_link_libraries(quantapdf_test_native_clip_dag_v3 PRIVATE m)
endif()
add_test(NAME quantapdf.native_clip_dag_v3
  COMMAND quantapdf_test_native_clip_dag_v3)
set_tests_properties(quantapdf.native_clip_dag_v3 PROPERTIES
  TIMEOUT 30 LABELS "conformance;native-raster")


# Test-only nested Clip DAG oracle: native C11 evaluation vs Writer + PDFium.
# Only this differential test links PDFium/qpdf, not the native DAG runtime.
add_executable(quantapdf_test_native_nested_clip_oracle
  test_ir_nested_clip_oracle.cpp
  "${PROJECT_SOURCE_DIR}/src/render/ir_clip_dag_v3.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
target_include_directories(quantapdf_test_native_nested_clip_oracle PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_native_nested_clip_oracle PRIVATE
  QuantaPDF::QuantaPDF quantapdf_backend QuantaPDF::PDFium qpdf::libqpdf)
target_compile_features(quantapdf_test_native_nested_clip_oracle PRIVATE
  c_std_11 cxx_std_20)
target_compile_definitions(quantapdf_test_native_nested_clip_oracle PRIVATE
  ORACLE_NESTED_PDF="${CMAKE_CURRENT_BINARY_DIR}/nested-oracle-nested.pdf"
  ORACLE_SHARED_PDF="${CMAKE_CURRENT_BINARY_DIR}/nested-oracle-shared.pdf"
  ORACLE_FRACTIONAL_PDF="${CMAKE_CURRENT_BINARY_DIR}/nested-oracle-fraction.pdf"
  ORACLE_HOLE_PDF="${CMAKE_CURRENT_BINARY_DIR}/nested-oracle-hole.pdf"
  ORACLE_CUBIC_PDF="${CMAKE_CURRENT_BINARY_DIR}/nested-oracle-cubic.pdf")
if(MSVC)
  target_compile_options(quantapdf_test_native_nested_clip_oracle PRIVATE /utf-8)
endif()
add_test(NAME quantapdf.native_nested_clip_oracle
  COMMAND quantapdf_test_native_nested_clip_oracle)
set_tests_properties(quantapdf.native_nested_clip_oracle PROPERTIES
  TIMEOUT 40 LABELS "conformance;native-raster")
if(WIN32)
  set_property(TEST quantapdf.native_nested_clip_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.native_nested_clip_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.native_nested_clip_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()


# Pure C11 multi-operation nested Clip DAG test, no PDFium/qpdf/runtime links.
add_executable(quantapdf_test_native_scoped_clip_dag_v1
  test_ir_scoped_clip_dag_v1.c
  "${PROJECT_SOURCE_DIR}/src/render/ir_scoped_operations_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_clip_dag_v3.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
target_include_directories(quantapdf_test_native_scoped_clip_dag_v1 PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(quantapdf_test_native_scoped_clip_dag_v1 PRIVATE c_std_11)
if(MSVC)
  target_compile_options(quantapdf_test_native_scoped_clip_dag_v1 PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_test_native_scoped_clip_dag_v1 PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
if(UNIX)
  target_link_libraries(quantapdf_test_native_scoped_clip_dag_v1 PRIVATE m)
endif()
add_test(NAME quantapdf.native_scoped_clip_dag_v1
  COMMAND quantapdf_test_native_scoped_clip_dag_v1)
set_tests_properties(quantapdf.native_scoped_clip_dag_v1 PROPERTIES
  TIMEOUT 30 LABELS "conformance;native-raster")


# Test-only QPDF/PDFium differential oracle for private scoped nested Clip DAG.
# Standalone native C11 scoped DAG test remains independent of both backends.
add_executable(quantapdf_test_native_scoped_dag_oracle
  test_ir_scoped_dag_oracle.cpp
  "${PROJECT_SOURCE_DIR}/src/render/ir_scoped_operations_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_clip_dag_v3.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
target_include_directories(quantapdf_test_native_scoped_dag_oracle PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_native_scoped_dag_oracle PRIVATE
  QuantaPDF::QuantaPDF quantapdf_backend QuantaPDF::PDFium qpdf::libqpdf)
target_compile_features(quantapdf_test_native_scoped_dag_oracle PRIVATE
  c_std_11 cxx_std_20)
target_compile_definitions(quantapdf_test_native_scoped_dag_oracle PRIVATE
  ORACLE_SCOPED_PDF="${CMAKE_CURRENT_BINARY_DIR}/dag-oracle-scoped.pdf"
  ORACLE_REVERSE_PDF="${CMAKE_CURRENT_BINARY_DIR}/dag-oracle-reverse.pdf"
  ORACLE_FRACTION_PDF="${CMAKE_CURRENT_BINARY_DIR}/dag-oracle-fraction.pdf")
if(MSVC)
  target_compile_options(quantapdf_test_native_scoped_dag_oracle PRIVATE /utf-8)
endif()
add_test(NAME quantapdf.native_scoped_dag_oracle
  COMMAND quantapdf_test_native_scoped_dag_oracle)
set_tests_properties(quantapdf.native_scoped_dag_oracle PROPERTIES
  TIMEOUT 40 LABELS "conformance;native-raster")
if(WIN32)
  set_property(TEST quantapdf.native_scoped_dag_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.native_scoped_dag_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.native_scoped_dag_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()


# Original source-PDF q/Q/W/W* -> detached Graph/Composer IR -> pure native
# scoped Clip DAG raster. PDFium is an independent TEST-ONLY original-PDF oracle.
add_executable(quantapdf_test_native_source_qq_oracle
  test_native_source_qq_oracle.cpp
  ../src/graph/graph_page_geometry.c
  ../src/document/page_composer_ir.c
  ../src/document/page_content_ir.c
  ../src/document/document_graph.c
  ../src/ir/pdf_path_lowering.c
  ../src/ir/pdf_graphics_state.c
  ../src/ir/pdf_content_ir.c
  ../src/ir/ir.c
  ../src/resource/resource_registry.c
  ../src/core/context_lifetime.c
  ../src/core/output_lifetime.c
  ../src/snapshot/snapshot_owner.c
  ../src/render/ir_scoped_operations_v1.c
  ../src/render/ir_clip_dag_v3.c
  ../src/render/ir_raster_v1.c
  ../src/render/ir_raster.c)
target_include_directories(quantapdf_test_native_source_qq_oracle PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_native_source_qq_oracle PRIVATE
  QuantaPDF::QuantaPDF quantapdf_backend QuantaPDF::PDFium qpdf::libqpdf)
target_compile_features(quantapdf_test_native_source_qq_oracle PRIVATE
  c_std_11 cxx_std_20)
target_compile_definitions(quantapdf_test_native_source_qq_oracle PRIVATE
  SOURCE_QQ_PDF="${CMAKE_CURRENT_BINARY_DIR}/native-qq-source.pdf"
  GROUP_SOURCE_PDF="${CMAKE_CURRENT_BINARY_DIR}/native-group-source.pdf"
  GENERATED_WSTAR_OBSERVE_PDF="${CMAKE_CURRENT_BINARY_DIR}/generated-wstar-typed-observe.pdf")
if(MSVC)
  target_compile_options(quantapdf_test_native_source_qq_oracle PRIVATE /utf-8)
endif()
add_test(NAME quantapdf.native_source_qq_oracle
  COMMAND quantapdf_test_native_source_qq_oracle
    --corpus-report "${CMAKE_CURRENT_BINARY_DIR}/native-source-corpus.json")
set_tests_properties(quantapdf.native_source_qq_oracle PROPERTIES
  TIMEOUT 40 LABELS "conformance;native-raster;source-pdf;source-corpus")
if(WIN32)
  set_property(TEST quantapdf.native_source_qq_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.native_source_qq_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.native_source_qq_oracle
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()


# Real source-PDF leaf /Group admission. Reuse the original PDFium test-only
# binary, but do NOT count its generated scenario fixtures as external PDFs
# or as members of the existing 14/36 source corpus manifest.
add_test(NAME quantapdf.native_page_group_admission
  COMMAND quantapdf_test_native_source_qq_oracle --page-group-admission)
set_tests_properties(quantapdf.native_page_group_admission PROPERTIES
  TIMEOUT 40 LABELS "conformance;native-raster;source-pdf;page-group")
if(WIN32)
  set_property(TEST quantapdf.native_page_group_admission
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.native_page_group_admission
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.native_page_group_admission
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# Independently published CC0 PDF source; test-only PDFium validates that
# the ORIGINAL file renders, while native Graph/IR rejects unsupported text.
# Reuse the same executable, but NOT the 14/36 generated-fixture registry.
add_test(NAME quantapdf.external_cc0_text_source
  COMMAND quantapdf_test_native_source_qq_oracle --external-pdf
    "${CMAKE_CURRENT_SOURCE_DIR}/external_source_corpus/documents/tinytoolkit.sample-1-page-v1.pdf"
    --external-report "${CMAKE_CURRENT_BINARY_DIR}/external-cc0-text-status.json")
set_tests_properties(quantapdf.external_cc0_text_source PROPERTIES
  TIMEOUT 40 LABELS "conformance;external-corpus;native-raster")
if(WIN32)
  set_property(TEST quantapdf.external_cc0_text_source
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.external_cc0_text_source
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.external_cc0_text_source
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# Independently published CC0 blank Letter PDF: a positive ORIGINAL-file
# white-composited native V0 raster oracle, distinct from future Path/Clip.
add_test(NAME quantapdf.external_cc0_blank_native
  COMMAND quantapdf_test_native_source_qq_oracle --external-blank-pdf
    "${CMAKE_CURRENT_SOURCE_DIR}/external_source_corpus/documents/tinytoolkit.sample-blank-letter-v1.pdf"
    --external-report "${CMAKE_CURRENT_BINARY_DIR}/external-cc0-blank-status.json")
set_tests_properties(quantapdf.external_cc0_blank_native PROPERTIES
  TIMEOUT 40 LABELS "conformance;external-corpus;native-raster;blank-page")
if(WIN32)
  set_property(TEST quantapdf.external_cc0_blank_native
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.external_cc0_blank_native
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.external_cc0_blank_native
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# The ORIGINAL upstream PDFium black.pdf contains one opaque black /Path
# fill with /ExtGState normal alpha. Independent source-PDFium pixel oracle.
add_test(NAME quantapdf.external_pdfium_black_path
  COMMAND quantapdf_test_native_source_qq_oracle --external-black-pdf
    "${CMAKE_CURRENT_SOURCE_DIR}/external_source_corpus/documents/pdfium.black-original-v1.pdf"
    --external-report "${CMAKE_CURRENT_BINARY_DIR}/external-pdfium-black-path-status.json")
set_tests_properties(quantapdf.external_pdfium_black_path PROPERTIES
  TIMEOUT 40 LABELS "conformance;external-corpus;native-raster;opaque-path")
if(WIN32)
  set_property(TEST quantapdf.external_pdfium_black_path
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.external_pdfium_black_path
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.external_pdfium_black_path
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# Independently published original PDFium W* + opaque f* with late text.
# This qualifies fail-closed UNSUPPORTED, never W* native pixel support.
add_test(NAME quantapdf.external_pdfium_wstar_late_text_rejection
  COMMAND quantapdf_test_native_source_qq_oracle --external-wstar-text-pdf
    "${CMAKE_CURRENT_SOURCE_DIR}/external_source_corpus/documents/pdfium.bug-1574-wstar-text-v1.pdf"
    --external-report "${CMAKE_CURRENT_BINARY_DIR}/external-pdfium-wstar-text-status.json")
set_tests_properties(quantapdf.external_pdfium_wstar_late_text_rejection PROPERTIES
  TIMEOUT 40 LABELS "conformance;external-corpus;native-raster;wstar-negative")
if(WIN32)
  set_property(TEST quantapdf.external_pdfium_wstar_late_text_rejection
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.external_pdfium_wstar_late_text_rejection
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.external_pdfium_wstar_late_text_rejection
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# Selected-page typed semantic preflight. Unlike the byte scanner, the
# existing Graph parser resolves the COMPLETE original selected page.
# This test-only gate NEVER claims W* pixel or public SDK support.
add_test(NAME quantapdf.external_original_w_typed_preflight
  COMMAND quantapdf_test_native_source_qq_oracle
    --preflight-original-pdf
      "${CMAKE_CURRENT_SOURCE_DIR}/external_source_corpus/documents/pdfium.clip-path-v1.pdf"
    --page-index 0 --expect-status lowered
    --preflight-report "${CMAKE_CURRENT_BINARY_DIR}/external-original-w-preflight.json")
add_test(NAME quantapdf.external_original_wstar_text_typed_preflight
  COMMAND quantapdf_test_native_source_qq_oracle
    --preflight-original-pdf
      "${CMAKE_CURRENT_SOURCE_DIR}/external_source_corpus/documents/pdfium.bug-1574-wstar-text-v1.pdf"
    --page-index 0 --expect-status unsupported
    --preflight-report "${CMAKE_CURRENT_BINARY_DIR}/external-original-wstar-preflight.json")

# Discovery-only typed observation: reuse the actual original/Graph/owned-IR
# chain and assert logged statuses, without attributing any native pixel pass.
add_test(NAME quantapdf.external_original_w_typed_observe
  COMMAND quantapdf_test_native_source_qq_oracle
    --preflight-original-pdf
      "${CMAKE_CURRENT_SOURCE_DIR}/external_source_corpus/documents/pdfium.clip-path-v1.pdf"
    --page-index 0 --expect-status observe
    --preflight-report "${CMAKE_CURRENT_BINARY_DIR}/external-original-w-observe.json")
set_tests_properties(quantapdf.external_original_w_typed_observe PROPERTIES
  PASS_REGULAR_EXPRESSION "typed=lowered raster=not_run")
add_test(NAME quantapdf.external_original_wstar_text_typed_observe
  COMMAND quantapdf_test_native_source_qq_oracle
    --preflight-original-pdf
      "${CMAKE_CURRENT_SOURCE_DIR}/external_source_corpus/documents/pdfium.bug-1574-wstar-text-v1.pdf"
    --page-index 0 --expect-status observe
    --preflight-report "${CMAKE_CURRENT_BINARY_DIR}/external-original-wstar-observe.json")
set_tests_properties(quantapdf.external_original_wstar_text_typed_observe PROPERTIES
  PASS_REGULAR_EXPRESSION "typed=unsupported raster=not_run")
# The test-only typed 'observe' CLI must preserve an existing report
# if a new candidate is malformed or contradicts strict preflight status.
add_test(NAME quantapdf.external_original_typed_report_guard
  COMMAND "${CMAKE_COMMAND}"
    "-DORACLE=$<TARGET_FILE:quantapdf_test_native_source_qq_oracle>"
    "-DW_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/external_source_corpus/documents/pdfium.clip-path-v1.pdf"
    "-DWSTAR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/external_source_corpus/documents/pdfium.bug-1574-wstar-text-v1.pdf"
    "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/typed-original-report-guard"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_native_source_preflight_report.cmake")

foreach(qp_test IN ITEMS
        quantapdf.external_original_w_typed_preflight
        quantapdf.external_original_wstar_text_typed_preflight
        quantapdf.external_original_w_typed_observe
        quantapdf.external_original_wstar_text_typed_observe
        quantapdf.external_original_typed_report_guard)
  set_tests_properties(${qp_test} PROPERTIES
    TIMEOUT 40 LABELS "conformance;external-corpus;source-preflight")
  if(WIN32)
    set_property(TEST ${qp_test} PROPERTY ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
  elseif(APPLE)
    set_property(TEST ${qp_test} PROPERTY ENVIRONMENT_MODIFICATION
      "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
  else()
    set_property(TEST ${qp_test} PROPERTY ENVIRONMENT_MODIFICATION
      "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
  endif()
endforeach()

# Deliberately GENERATED observation-only control for the real bounded
# Document->Graph->detached IR W* leaf report. This must not enter the
# five-original external manifest or the fixed generated 14/36 counters.
add_test(NAME quantapdf.generated_wstar_typed_observe
  COMMAND quantapdf_test_native_source_qq_oracle
    --generated-wstar-observe
    --preflight-report "${CMAKE_CURRENT_BINARY_DIR}/generated-wstar-typed-observe.json")
set_tests_properties(quantapdf.generated_wstar_typed_observe PROPERTIES
  TIMEOUT 40
  LABELS "conformance;source-preflight;generated-observation"
  PASS_REGULAR_EXPRESSION "generated-wstar-observe.*external_positive=0")
if(WIN32)
  set_property(TEST quantapdf.generated_wstar_typed_observe
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.generated_wstar_typed_observe
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.generated_wstar_typed_observe
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# Byte-pinned W-only originals, quarantined from the W* positive population.
# The actual selected-page Document -> Graph -> detached IR observe is used
# instead of heuristic token scanning. No pixel parity or corpus admission.
function(add_external_nonzero_candidate_observe case_id relative_pdf expected_sha256 expected_size)
  set(optional_paint_budget)
  if(ARGC GREATER 4)
    set(optional_paint_budget "-DEXPLICIT_MAX_PAINTED=${ARGV4}")
  endif()
  add_test(NAME quantapdf.external_${case_id}_typed_observe
    COMMAND "${CMAKE_COMMAND}"
      "-DORACLE=$<TARGET_FILE:quantapdf_test_native_source_qq_oracle>"
      "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/${relative_pdf}"
      "-DREPORT=${CMAKE_CURRENT_BINARY_DIR}/external-${case_id}-typed-observe.json"
      "-DEXPECTED_SHA256=${expected_sha256}"
      "-DEXPECTED_SIZE=${expected_size}"
      "-DCASE_ID=${case_id}"
      ${optional_paint_budget}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/check_external_nonzero_candidate_observe.cmake")
  set_tests_properties(quantapdf.external_${case_id}_typed_observe PROPERTIES
    TIMEOUT 40 LABELS "conformance;source-preflight;nonzero-candidate"
    PASS_REGULAR_EXPRESSION "external W-only candidate:.*typed=")
  if(WIN32)
    set_property(TEST quantapdf.external_${case_id}_typed_observe
      PROPERTY ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
  elseif(APPLE)
    set_property(TEST quantapdf.external_${case_id}_typed_observe
      PROPERTY ENVIRONMENT_MODIFICATION
      "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
  else()
    set_property(TEST quantapdf.external_${case_id}_typed_observe
      PROPERTY ENVIRONMENT_MODIFICATION
      "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
  endif()
endfunction()
add_external_nonzero_candidate_observe(
  yworks-svg-nested-w external_wstar_candidates/documents/yworks.clippath-svg-v1.pdf
  7c9d3eab0f401537052a5ec4a8c531954aaf354061a9de25080376a9e13cb921 4107)
add_external_nonzero_candidate_observe(
  yworks-hidden-w external_wstar_candidates/documents/yworks.hidden-clippath-v1.pdf
  6369893b1f5bd57790f098242d49775094d6c25a1a9b2316aaf1fd1f9a39bc71 3309)
add_external_nonzero_candidate_observe(
  jspdf-w3s-clip external_wstar_candidates/documents/jspdf.w3s-clip-v1.pdf
  179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e 3344)

add_external_nonzero_candidate_observe(
  yworks-svg-nested-w-budget8 external_wstar_candidates/documents/yworks.clippath-svg-v1.pdf
  7c9d3eab0f401537052a5ec4a8c531954aaf354061a9de25080376a9e13cb921 4107 8)

# Test-only explicit stage diagnostic on one raw byte-pinned, still
# unsupported nested-W original; never a positive or backend switch.
add_test(NAME quantapdf.external_yworks_svg_w_stage_diagnostic
  COMMAND "${CMAKE_COMMAND}"
    "-DORACLE=$<TARGET_FILE:quantapdf_test_native_source_qq_oracle>"
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/external_wstar_candidates/documents/yworks.clippath-svg-v1.pdf"
    "-DREPORT=${CMAKE_CURRENT_BINARY_DIR}/external-yworks-svg-w-stage-diagnostic.json"
    "-DEXPECTED_SHA256=7c9d3eab0f401537052a5ec4a8c531954aaf354061a9de25080376a9e13cb921"
    "-DEXPECTED_SIZE=4107"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_external_nonzero_stage_diagnostic.cmake")
set_tests_properties(quantapdf.external_yworks_svg_w_stage_diagnostic PROPERTIES
  TIMEOUT 40 LABELS "conformance;source-preflight;nonzero-stage-diagnostic"
  PASS_REGULAR_EXPRESSION "original-stage-diag.*status=lowered")
if(WIN32)
  set_property(TEST quantapdf.external_yworks_svg_w_stage_diagnostic
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.external_yworks_svg_w_stage_diagnostic
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.external_yworks_svg_w_stage_diagnostic
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# Original jsPDF producer W-only Clip with fractional A4 page points:
# typed lower succeeds, but private C11 pixel grid has not qualified
# fractional MediaBox→pixel mapping. Fail closed; no QPDF/PDFium fallback.
add_test(NAME quantapdf.external_jspdf_fractional_page_native_rejection
  COMMAND "${CMAKE_COMMAND}"
    "-DORACLE=$<TARGET_FILE:quantapdf_test_native_source_qq_oracle>"
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/external_wstar_candidates/documents/jspdf.w3s-clip-v1.pdf"
    "-DREPORT=${CMAKE_CURRENT_BINARY_DIR}/external-jspdf-fractional-page-native.json"
    "-DEXPECTED_SHA256=179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_external_jspdf_fractional_page.cmake")
set_tests_properties(quantapdf.external_jspdf_fractional_page_native_rejection PROPERTIES
  TIMEOUT 40 LABELS "conformance;source-preflight;fractional-page-negative"
  PASS_REGULAR_EXPRESSION "fractional-page-jsPDF: typed=lowered raster=UNSUPPORTED")
if(WIN32)
  set_property(TEST quantapdf.external_jspdf_fractional_page_native_rejection
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.external_jspdf_fractional_page_native_rejection
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.external_jspdf_fractional_page_native_rejection
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# Authentic, byte-pinned upstream W* PDF typed candidate, isolated from
# five admitted original conformance sources and all native pixel counters.
add_test(NAME quantapdf.external_yworks_wstar_typed_observe
  COMMAND "${CMAKE_COMMAND}"
    "-DORACLE=$<TARGET_FILE:quantapdf_test_native_source_qq_oracle>"
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/external_wstar_candidates/documents/yworks.clippath-cliprule-v1.pdf"
    "-DREPORT=${CMAKE_CURRENT_BINARY_DIR}/yworks-wstar-typed-observe.json"
    "-DEXPECTED_SHA256=9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_external_wstar_candidate_observe.cmake")
set_tests_properties(quantapdf.external_yworks_wstar_typed_observe PROPERTIES
  TIMEOUT 40 LABELS "conformance;source-preflight;wstar-candidate"
  PASS_REGULAR_EXPRESSION "yworks Wstar original candidate: typed=")
if(WIN32)
  set_property(TEST quantapdf.external_yworks_wstar_typed_observe
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.external_yworks_wstar_typed_observe
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.external_yworks_wstar_typed_observe
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# Independently published yWorks complex W-only SVG PDF. Six original
# painted Paths, five W leaves, nine total Clip nodes. Test-only original
# PDFium vs detached pure C11; NEVER modifies formal six-source counter.
add_test(NAME quantapdf.external_yworks_svg_w_candidate_pixels
  COMMAND "${CMAKE_COMMAND}"
    "-DORACLE=$<TARGET_FILE:quantapdf_test_native_source_qq_oracle>"
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/external_wstar_candidates/documents/yworks.clippath-svg-v1.pdf"
    "-DREPORT=${CMAKE_CURRENT_BINARY_DIR}/yworks-svg-w-candidate-pixel.json"
    "-DEXPECTED_SHA256=7c9d3eab0f401537052a5ec4a8c531954aaf354061a9de25080376a9e13cb921"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_external_svg_w_candidate_pixel.cmake")
set_tests_properties(quantapdf.external_yworks_svg_w_candidate_pixels PROPERTIES
  TIMEOUT 200 LABELS "conformance;native-raster;nonzero-pixel-candidate"
  PASS_REGULAR_EXPRESSION "svg-W-original-pixel: passed")
if(WIN32)
  set_property(TEST quantapdf.external_yworks_svg_w_candidate_pixels
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.external_yworks_svg_w_candidate_pixels
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.external_yworks_svg_w_candidate_pixels
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# Candidate-only unchanged upstream yWorks hidden W Clip, independently
# checked against ORIGINAL PDFium pixels. Never participates in official
# six-source corpus counters or the separate W* numerator.
add_test(NAME quantapdf.external_yworks_hidden_w_candidate_pixels
  COMMAND "${CMAKE_COMMAND}"
    "-DORACLE=$<TARGET_FILE:quantapdf_test_native_source_qq_oracle>"
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/external_wstar_candidates/documents/yworks.hidden-clippath-v1.pdf"
    "-DREPORT=${CMAKE_CURRENT_BINARY_DIR}/yworks-hidden-w-candidate-pixel.json"
    "-DEXPECTED_SHA256=6369893b1f5bd57790f098242d49775094d6c25a1a9b2316aaf1fd1f9a39bc71"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_external_hidden_w_candidate_pixel.cmake")
set_tests_properties(quantapdf.external_yworks_hidden_w_candidate_pixels PROPERTIES
  TIMEOUT 90 LABELS "conformance;native-raster;nonzero-pixel-candidate"
  PASS_REGULAR_EXPRESSION "candidate-hidden-W-pixel: passed")
if(WIN32)
  set_property(TEST quantapdf.external_yworks_hidden_w_candidate_pixels
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.external_yworks_hidden_w_candidate_pixels
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.external_yworks_hidden_w_candidate_pixels
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# Isolated original PDFium-vs-C11 candidate W* pixel oracle, not yet
# admitted to the five-original corpus or the production SDK.
add_test(NAME quantapdf.external_yworks_wstar_candidate_pixels
  COMMAND "${CMAKE_COMMAND}"
    "-DORACLE=$<TARGET_FILE:quantapdf_test_native_source_qq_oracle>"
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/external_wstar_candidates/documents/yworks.clippath-cliprule-v1.pdf"
    "-DREPORT=${CMAKE_CURRENT_BINARY_DIR}/yworks-wstar-candidate-pixel.json"
    "-DEXPECTED_SHA256=9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_external_wstar_candidate_pixel.cmake")
set_tests_properties(quantapdf.external_yworks_wstar_candidate_pixels PROPERTIES
  TIMEOUT 80 LABELS "conformance;native-raster;wstar-pixel-candidate"
  PASS_REGULAR_EXPRESSION "yworks original Wstar candidate independent PDFium/C11 pixel parity: passed")
if(WIN32)
  set_property(TEST quantapdf.external_yworks_wstar_candidate_pixels
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.external_yworks_wstar_candidate_pixels
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.external_yworks_wstar_candidate_pixels
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# Independently released, MIT-licensed, SHA-256-pinned W/W* original.
# The official six-source reconciler is the ONLY place that counts evidence.
add_test(NAME quantapdf.external_yworks_wstar_original_native
  COMMAND "${CMAKE_COMMAND}"
    "-DORACLE=$<TARGET_FILE:quantapdf_test_native_source_qq_oracle>"
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/external_source_corpus/documents/yworks.clippath-cliprule-v1.pdf"
    "-DREPORT=${CMAKE_CURRENT_BINARY_DIR}/external-yworks-wstar-native-status.json"
    "-DEXPECTED_SHA256=9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_external_wstar_admitted_pixel.cmake")
set_tests_properties(quantapdf.external_yworks_wstar_original_native PROPERTIES
  TIMEOUT 80 LABELS "conformance;external-corpus;native-raster;wstar-positive"
  PASS_REGULAR_EXPRESSION "admitted Wstar original: PDFium/C11 parity PASSED")
if(WIN32)
  set_property(TEST quantapdf.external_yworks_wstar_original_native
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.external_yworks_wstar_original_native
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.external_yworks_wstar_original_native
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# Failure-path evidence gate for BOTH independent original-PDFium/C11
# W* wrappers. Invalid-but-SHA-pinned source must preserve a prior report;
# source/output hardlink aliases must be rejected before any pixel writes.
add_test(NAME quantapdf.external_wstar_pixel_report_guard
  COMMAND "${CMAKE_COMMAND}"
    "-DORACLE=$<TARGET_FILE:quantapdf_test_native_source_qq_oracle>"
    "-DORIGINAL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/external_source_corpus/documents/yworks.clippath-cliprule-v1.pdf"
    "-DADMITTED_GUARD=${CMAKE_CURRENT_SOURCE_DIR}/check_external_wstar_admitted_pixel.cmake"
    "-DCANDIDATE_GUARD=${CMAKE_CURRENT_SOURCE_DIR}/check_external_wstar_candidate_pixel.cmake"
    "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/external-wstar-pixel-report-guard"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_external_wstar_pixel_report_guard.cmake")
set_tests_properties(quantapdf.external_wstar_pixel_report_guard PROPERTIES
  TIMEOUT 40 LABELS "conformance;wstar-pixel-report-guard"
  PASS_REGULAR_EXPRESSION "wstar-pixel-report-guard: 4 fail-closed cases passed")
if(WIN32)
  set_property(TEST quantapdf.external_wstar_pixel_report_guard
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.external_wstar_pixel_report_guard
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.external_wstar_pixel_report_guard
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# Independently published upstream W n rectangular Clip and one-pixel closed
# blue stroke. Renderer uses pure C11; PDFium reads the ORIGINAL PDF only.
add_test(NAME quantapdf.external_pdfium_clipped_stroke
  COMMAND quantapdf_test_native_source_qq_oracle --external-clip-stroke-pdf
    "${CMAKE_CURRENT_SOURCE_DIR}/external_source_corpus/documents/pdfium.clip-path-v1.pdf"
    --external-report "${CMAKE_CURRENT_BINARY_DIR}/external-pdfium-clip-stroke-status.json")
set_tests_properties(quantapdf.external_pdfium_clipped_stroke PROPERTIES
  TIMEOUT 40 LABELS "conformance;external-corpus;native-raster;clip-stroke")
if(WIN32)
  set_property(TEST quantapdf.external_pdfium_clipped_stroke
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
elseif(APPLE)
  set_property(TEST quantapdf.external_pdfium_clipped_stroke
    PROPERTY ENVIRONMENT_MODIFICATION
    "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
else()
  set_property(TEST quantapdf.external_pdfium_clipped_stroke
    PROPERTY ENVIRONMENT_MODIFICATION
    "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
endif()

# Strict private native-device grid: no PDFium/QPDF or data fixture.
add_executable(quantapdf_test_native_device_grid
  test_native_device_grid.c ../src/render/native_device_grid.c)
target_include_directories(quantapdf_test_native_device_grid PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
set_target_properties(quantapdf_test_native_device_grid PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
  target_compile_options(quantapdf_test_native_device_grid PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_test_native_device_grid PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
if(UNIX)
  target_link_libraries(quantapdf_test_native_device_grid PRIVATE m)
endif()
add_test(NAME quantapdf.native_device_grid COMMAND quantapdf_test_native_device_grid)
set_tests_properties(quantapdf.native_device_grid PROPERTIES
  TIMEOUT 20 LABELS "conformance;native-grid;geometry-only")
