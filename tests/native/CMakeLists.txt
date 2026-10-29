# Native-only conformance: no PDFium link, headers, runtime or oracle.
# The full public renderer has a strict Source V10 subset; unsupported
# operations are tested as errors, not redirected to another backend.
function(quantapdf_native_test target source)
  add_executable(${target} "${PROJECT_SOURCE_DIR}/tests/${source}" ${ARGN})
  target_include_directories(${target} PRIVATE
    "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
  target_compile_features(${target} PRIVATE c_std_11)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  if(UNIX)
    target_link_libraries(${target} PRIVATE m)
  endif()
  add_test(NAME ${target} COMMAND ${target})
  set_tests_properties(${target} PROPERTIES TIMEOUT 40
    LABELS "conformance;native;no-pdfium")
endfunction()

quantapdf_native_test(quantapdf_native_device_plan
  test_native_device_plan.c
  "${PROJECT_SOURCE_DIR}/src/render/native_device_plan.c")
quantapdf_native_test(quantapdf_native_clip_dag
  test_ir_clip_dag_v3.c
  "${PROJECT_SOURCE_DIR}/src/render/ir_clip_dag_v3.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
quantapdf_native_test(quantapdf_native_scoped_mixed
  test_ir_raster_scoped_mixed_v5.c
  $<TARGET_OBJECTS:quantapdf_native_raster_objects>
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_plan.c")
quantapdf_native_test(quantapdf_native_device_ir
  test_native_device_ir_v1.c
  "${PROJECT_SOURCE_DIR}/src/render/native_device_ir_v1.c"
  $<TARGET_OBJECTS:quantapdf_native_raster_objects>
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_plan.c")
quantapdf_native_test(quantapdf_native_rgb
  test_native_backend_rgb_v1.c
  $<TARGET_OBJECTS:quantapdf_native_rgb_objects>)
quantapdf_native_test(quantapdf_native_raw_images
  test_ir_raster_image_v1.c
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_image_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")

# Public C11 render_page must use native pixels from a real, qpdf-authored
# PDF; the test owns its exact RGB goldens and checks unsupported input.
# It NEVER imports PDFium and never compares two rendering engines.
add_executable(quantapdf_native_public_render test_public_native_render.cpp)
target_compile_features(quantapdf_native_public_render PRIVATE cxx_std_20)
target_link_libraries(quantapdf_native_public_render PRIVATE
  QuantaPDF::QuantaPDF qpdf::libqpdf ZLIB::ZLIB)
if(MSVC)
  target_compile_options(quantapdf_native_public_render PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_public_render PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_public_render
  COMMAND quantapdf_native_public_render)
set_tests_properties(quantapdf.native_public_render PROPERTIES
  TIMEOUT 45 LABELS "conformance;native;no-pdfium;public-api")


# TEST-ONLY Context OOM hook for the real Native V10 public Bitmap handoff.
# QPDF creates only the ORIGINAL source; no PDFium, second raster or SDK ABI.
# The independent installed C11 consumer remains public-header-only.
add_executable(quantapdf_native_userunit_oom test_native_userunit_oom.cpp)
target_compile_features(quantapdf_native_userunit_oom PRIVATE cxx_std_20)
target_include_directories(quantapdf_native_userunit_oom PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(quantapdf_native_userunit_oom PRIVATE
  QUANTAPDF_TESTING=1)
target_link_libraries(quantapdf_native_userunit_oom PRIVATE
  QuantaPDF::QuantaPDF qpdf::libqpdf)
if(MSVC)
  target_compile_options(quantapdf_native_userunit_oom PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_userunit_oom PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_userunit_oom
  COMMAND quantapdf_native_userunit_oom)
set_tests_properties(quantapdf.native_userunit_oom PROPERTIES
  TIMEOUT 30 LABELS "conformance;native;no-pdfium;oom;private-test-hook")


# Exact production C++ Source parser, independently compiled ONLY in
# this in-tree fault test. Its guarded stage probes are elided from
# the normal library and installed SDK. No PDFium or fallback.
add_executable(quantapdf_native_source_stage_oom
  test_native_source_stage_oom.cpp
  "${PROJECT_SOURCE_DIR}/src/backend/qpdf_native_image_ir.cpp"
  "${PROJECT_SOURCE_DIR}/src/ir/ir.c"
  "${PROJECT_SOURCE_DIR}/src/resource/resource_registry.c")
target_compile_definitions(quantapdf_native_source_stage_oom PRIVATE
  QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST=1)
target_include_directories(quantapdf_native_source_stage_oom PRIVATE
  "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
target_compile_features(quantapdf_native_source_stage_oom PRIVATE
  c_std_11 cxx_std_20)
target_link_libraries(quantapdf_native_source_stage_oom PRIVATE
  quantapdf_backend QuantaPDF::QuantaPDF qpdf::libqpdf)
if(MSVC)
  target_compile_options(quantapdf_native_source_stage_oom PRIVATE
    /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_source_stage_oom PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_source_stage_oom
  COMMAND quantapdf_native_source_stage_oom)
set_tests_properties(quantapdf.native_source_stage_oom PROPERTIES
  TIMEOUT 40 LABELS "conformance;native;no-pdfium;source-staging;oom")

# Public Source V10 standard PDF 'n': discard a bounded path without
# ink, Clip or a new C11 layer. Original QPDF PDFs, no provider fallback
# and unchanged private V8/V9 source contracts.
add_executable(quantapdf_native_unpainted_n
  test_native_unpainted_n.cpp)
target_compile_features(quantapdf_native_unpainted_n PRIVATE cxx_std_20)
target_link_libraries(quantapdf_native_unpainted_n PRIVATE
  QuantaPDF::QuantaPDF qpdf::libqpdf)
if(MSVC)
  target_compile_options(quantapdf_native_unpainted_n PRIVATE
    /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_unpainted_n PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_unpainted_n
  COMMAND quantapdf_native_unpainted_n)
set_tests_properties(quantapdf.native_unpainted_n PROPERTIES
  TIMEOUT 35 LABELS "conformance;native;no-pdfium;public-api;path")

# Original PDF re signed dimensions; immutable Source m/l/l/l/h winding
# and one pure-C11 V5 raster. No PDFium and no source provider fallback.
add_executable(quantapdf_native_signed_re test_native_signed_re.cpp)
target_compile_features(quantapdf_native_signed_re PRIVATE cxx_std_20)
target_link_libraries(quantapdf_native_signed_re PRIVATE
  QuantaPDF::QuantaPDF qpdf::libqpdf)
if(MSVC)
  target_compile_options(quantapdf_native_signed_re PRIVATE
    /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_signed_re PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_signed_re COMMAND quantapdf_native_signed_re)
set_tests_properties(quantapdf.native_signed_re PROPERTIES
  TIMEOUT 45 LABELS "conformance;native;no-pdfium;public-api;path")

# Strict stroking-only w/J/j source graphics state on public V10 Fill/
# RAW Image pages: no invisible Stroke painter, PDFium or second raster.
add_executable(quantapdf_native_stroke_state test_native_stroke_state.cpp)
target_compile_features(quantapdf_native_stroke_state PRIVATE cxx_std_20)
target_link_libraries(quantapdf_native_stroke_state PRIVATE
  QuantaPDF::QuantaPDF qpdf::libqpdf)
if(MSVC)
  target_compile_options(quantapdf_native_stroke_state PRIVATE
    /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_stroke_state PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_stroke_state
  COMMAND quantapdf_native_stroke_state)
set_tests_properties(quantapdf.native_stroke_state PROPERTIES
  TIMEOUT 45 LABELS "conformance;native;no-pdfium;public-api;graphics-state")

# Strict original PDF M/d (stroke-only) metadata on public Native V10
# Fill/RAW Image pages; no stroke producer or fallback.
add_executable(quantapdf_native_miter_dash test_native_miter_dash.cpp)
target_compile_features(quantapdf_native_miter_dash PRIVATE cxx_std_20)
target_link_libraries(quantapdf_native_miter_dash PRIVATE
  QuantaPDF::QuantaPDF qpdf::libqpdf)
if(MSVC)
  target_compile_options(quantapdf_native_miter_dash PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_miter_dash PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_miter_dash COMMAND quantapdf_native_miter_dash)
set_tests_properties(quantapdf.native_miter_dash PROPERTIES
  TIMEOUT 45 LABELS "conformance;native;no-pdfium;public-api;graphics-state")

# Public Source V10 stroking-only DeviceGray/RGB/CMYK state.
# One source producer, one C11 raster, no fallback or PDFium.
add_executable(quantapdf_native_stroke_color test_native_stroke_color.cpp)
target_compile_features(quantapdf_native_stroke_color PRIVATE cxx_std_20)
target_link_libraries(quantapdf_native_stroke_color PRIVATE
  QuantaPDF::QuantaPDF qpdf::libqpdf)
if(MSVC)
  target_compile_options(quantapdf_native_stroke_color PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_stroke_color PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_stroke_color COMMAND quantapdf_native_stroke_color)
set_tests_properties(quantapdf.native_stroke_color PROPERTIES
  TIMEOUT 45 LABELS "conformance;native;no-pdfium;public-api;graphics-state")

# Integration of six independent Native V10 grammar changes: original
# QPDF-authored cross-feature positive and atomic refusal, no second raster.
add_executable(quantapdf_native_integrated_v10 test_native_integrated_v10.cpp)
target_compile_features(quantapdf_native_integrated_v10 PRIVATE cxx_std_20)
target_link_libraries(quantapdf_native_integrated_v10 PRIVATE
  QuantaPDF::QuantaPDF qpdf::libqpdf)
if(MSVC)
  target_compile_options(quantapdf_native_integrated_v10 PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_integrated_v10 PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_integrated_v10 COMMAND quantapdf_native_integrated_v10)
set_tests_properties(quantapdf.native_integrated_v10 PROPERTIES
  TIMEOUT 45 LABELS "conformance;native;no-pdfium;public-api;integration")

# Public native Source V10 authored cubic Bezier c -> owned Composer IR
# -> the pre-existing pure-C11 bounded cubic Fill flattener (no PDFium).
add_executable(quantapdf_native_cubic_fill test_native_cubic_fill.cpp)
target_compile_features(quantapdf_native_cubic_fill PRIVATE cxx_std_20)
target_link_libraries(quantapdf_native_cubic_fill PRIVATE
  QuantaPDF::QuantaPDF qpdf::libqpdf)
if(MSVC)
  target_compile_options(quantapdf_native_cubic_fill PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_cubic_fill PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_cubic_fill COMMAND quantapdf_native_cubic_fill)
set_tests_properties(quantapdf.native_cubic_fill PROPERTIES
  TIMEOUT 45 LABELS "conformance;native;no-pdfium;public-api;cubic-fill")

# Stacked Source V10 native authored cubic c/v/y one-W/W* Clip
# geometry via the SAME C11 V5 raster; no PDFium/alternate pixel path.
add_executable(quantapdf_native_curved_clip test_native_curved_clip.cpp)
target_compile_features(quantapdf_native_curved_clip PRIVATE cxx_std_20)
target_link_libraries(quantapdf_native_curved_clip PRIVATE
  QuantaPDF::QuantaPDF qpdf::libqpdf)
if(MSVC)
  target_compile_options(quantapdf_native_curved_clip PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_curved_clip PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_curved_clip COMMAND quantapdf_native_curved_clip)
set_tests_properties(quantapdf.native_curved_clip PROPERTIES
  TIMEOUT 60 LABELS "conformance;native;no-pdfium;public-api;curved-clip")

# Private C11 V5 two-owned-Clip Image/PATH conformance. Source V10
# selects this same raster after publishing its bounded owned graph.
quantapdf_native_test(quantapdf_native_two_owned_clip_v5
  native/test_native_two_owned_clip_v5.c
  $<TARGET_OBJECTS:quantapdf_native_raster_objects>
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_plan.c")

# Private two-owned-Clip true-DPI projection into the same V5 raster.
# The public Source fixtures below qualify the original-PDF producer.
quantapdf_native_test(quantapdf_native_two_clip_device
  native/test_native_two_clip_device.c
  "${PROJECT_SOURCE_DIR}/src/render/native_device_ir_v1.c"
  $<TARGET_OBJECTS:quantapdf_native_raster_objects>
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_plan.c")
# Original PDF two-leaf Clip ownership through the public render API.
add_executable(quantapdf_native_two_curved_source test_native_two_curved_source.cpp)
target_compile_features(quantapdf_native_two_curved_source PRIVATE cxx_std_20)
target_link_libraries(quantapdf_native_two_curved_source PRIVATE
  QuantaPDF::QuantaPDF qpdf::libqpdf)
if(MSVC)
  target_compile_options(quantapdf_native_two_curved_source PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_two_curved_source PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_two_curved_source
  COMMAND quantapdf_native_two_curved_source)
set_tests_properties(quantapdf.native_two_curved_source PROPERTIES
  TIMEOUT 60 LABELS "conformance;native;no-pdfium;public-api;two-clip")

# Exact Source materializer with test-only returning-NULL allocation faults.
add_executable(quantapdf_native_two_clip_source_oom
  test_native_two_clip_source_oom.cpp
  "${PROJECT_SOURCE_DIR}/src/backend/qpdf_native_image_ir.cpp"
  "${PROJECT_SOURCE_DIR}/src/ir/ir.c"
  "${PROJECT_SOURCE_DIR}/src/resource/resource_registry.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_ir_v1.c"
  $<TARGET_OBJECTS:quantapdf_native_raster_objects>
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_plan.c")
target_compile_definitions(quantapdf_native_two_clip_source_oom PRIVATE
  QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST=1)
target_include_directories(quantapdf_native_two_clip_source_oom PRIVATE
  "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
target_compile_features(quantapdf_native_two_clip_source_oom PRIVATE
  c_std_11 cxx_std_20)
target_link_libraries(quantapdf_native_two_clip_source_oom PRIVATE
  quantapdf_backend QuantaPDF::QuantaPDF qpdf::libqpdf)
if(UNIX)
  target_link_libraries(quantapdf_native_two_clip_source_oom PRIVATE m)
endif()
if(MSVC)
  target_compile_options(quantapdf_native_two_clip_source_oom PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_two_clip_source_oom PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_two_clip_source_oom
  COMMAND quantapdf_native_two_clip_source_oom)
set_tests_properties(quantapdf.native_two_clip_source_oom PROPERTIES
  TIMEOUT 60 LABELS "conformance;native;no-pdfium;source;two-clip;oom")

# Explicit PATH-only Clip projection reuses the canonical bounded C11
# sampler while preserving older device and V5 admission contracts.
quantapdf_native_test(quantapdf_native_clipped_path_device
  native/test_native_clipped_path_device.c
  "${PROJECT_SOURCE_DIR}/src/render/native_device_ir_v1.c"
  $<TARGET_OBJECTS:quantapdf_native_raster_objects>
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_plan.c")

# Original PDFs containing only bounded opaque PATH fills under Clip.
add_executable(quantapdf_native_clipped_path_source
  test_native_clipped_path_source.cpp)
target_compile_features(quantapdf_native_clipped_path_source PRIVATE cxx_std_20)
target_link_libraries(quantapdf_native_clipped_path_source PRIVATE
  QuantaPDF::QuantaPDF qpdf::libqpdf)
if(MSVC)
  target_compile_options(quantapdf_native_clipped_path_source PRIVATE
    /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_clipped_path_source PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_clipped_path_source
  COMMAND quantapdf_native_clipped_path_source)
set_tests_properties(quantapdf.native_clipped_path_source PROPERTIES
  TIMEOUT 60 LABELS "conformance;native;no-pdfium;public-api;clipped-path")

# Exercise the actual Source owner at every PATH/GS/Clip C allocation.
add_executable(quantapdf_native_clipped_path_source_oom
  test_native_clipped_path_source_oom.cpp
  "${PROJECT_SOURCE_DIR}/src/backend/qpdf_native_image_ir.cpp"
  "${PROJECT_SOURCE_DIR}/src/ir/ir.c"
  "${PROJECT_SOURCE_DIR}/src/resource/resource_registry.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_ir_v1.c"
  $<TARGET_OBJECTS:quantapdf_native_raster_objects>
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_plan.c")
target_compile_definitions(quantapdf_native_clipped_path_source_oom PRIVATE
  QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST=1)
target_include_directories(quantapdf_native_clipped_path_source_oom PRIVATE
  "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
target_compile_features(quantapdf_native_clipped_path_source_oom PRIVATE
  c_std_11 cxx_std_20)
target_link_libraries(quantapdf_native_clipped_path_source_oom PRIVATE
  quantapdf_backend QuantaPDF::QuantaPDF qpdf::libqpdf)
if(UNIX)
  target_link_libraries(quantapdf_native_clipped_path_source_oom PRIVATE m)
endif()
if(MSVC)
  target_compile_options(quantapdf_native_clipped_path_source_oom PRIVATE
    /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_clipped_path_source_oom PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_clipped_path_source_oom
  COMMAND quantapdf_native_clipped_path_source_oom)
set_tests_properties(quantapdf.native_clipped_path_source_oom PROPERTIES
  TIMEOUT 60 LABELS "conformance;native;no-pdfium;source;clipped-path;oom")

# Original bounded LINE/CUBIC/empty Clip pairs with the same C11 sampler.
add_executable(quantapdf_native_bounded_clip_pairs
  test_native_bounded_clip_pairs.cpp)
target_compile_features(quantapdf_native_bounded_clip_pairs PRIVATE cxx_std_20)
target_link_libraries(quantapdf_native_bounded_clip_pairs PRIVATE
  QuantaPDF::QuantaPDF qpdf::libqpdf)
if(MSVC)
  target_compile_options(quantapdf_native_bounded_clip_pairs PRIVATE
    /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_bounded_clip_pairs PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_bounded_clip_pairs
  COMMAND quantapdf_native_bounded_clip_pairs)
set_tests_properties(quantapdf.native_bounded_clip_pairs PROPERTIES
  TIMEOUT 60 LABELS "conformance;native;no-pdfium;public-api;two-clip")

# Exact original Source ownership and rollback for line/empty Clip pairs.
add_executable(quantapdf_native_bounded_clip_pairs_oom
  test_native_bounded_clip_pairs_oom.cpp
  "${PROJECT_SOURCE_DIR}/src/backend/qpdf_native_image_ir.cpp"
  "${PROJECT_SOURCE_DIR}/src/ir/ir.c"
  "${PROJECT_SOURCE_DIR}/src/resource/resource_registry.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_ir_v1.c"
  $<TARGET_OBJECTS:quantapdf_native_raster_objects>
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_plan.c")
target_compile_definitions(quantapdf_native_bounded_clip_pairs_oom PRIVATE
  QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST=1)
target_include_directories(quantapdf_native_bounded_clip_pairs_oom PRIVATE
  "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
target_compile_features(quantapdf_native_bounded_clip_pairs_oom PRIVATE
  c_std_11 cxx_std_20)
target_link_libraries(quantapdf_native_bounded_clip_pairs_oom PRIVATE
  quantapdf_backend QuantaPDF::QuantaPDF qpdf::libqpdf)
if(UNIX)
  target_link_libraries(quantapdf_native_bounded_clip_pairs_oom PRIVATE m)
endif()
if(MSVC)
  target_compile_options(quantapdf_native_bounded_clip_pairs_oom PRIVATE
    /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_bounded_clip_pairs_oom PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_bounded_clip_pairs_oom
  COMMAND quantapdf_native_bounded_clip_pairs_oom)
set_tests_properties(quantapdf.native_bounded_clip_pairs_oom PROPERTIES
  TIMEOUT 60 LABELS "conformance;native;no-pdfium;source;two-clip;oom")

# Two distinct frozen Clip states use the same bounded C11 compositor.
quantapdf_native_test(quantapdf_native_clip_states_device
  native/test_native_clip_states_device.c
  "${PROJECT_SOURCE_DIR}/src/render/native_device_ir_v1.c"
  $<TARGET_OBJECTS:quantapdf_native_raster_objects>
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_plan.c")

# Original q/Q restores the first Clip independently of its intersection.
add_executable(quantapdf_native_clip_states_source
  test_native_clip_states_source.cpp)
target_compile_features(quantapdf_native_clip_states_source PRIVATE cxx_std_20)
target_link_libraries(quantapdf_native_clip_states_source PRIVATE
  QuantaPDF::QuantaPDF qpdf::libqpdf)
if(MSVC)
  target_compile_options(quantapdf_native_clip_states_source PRIVATE
    /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_clip_states_source PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_clip_states_source
  COMMAND quantapdf_native_clip_states_source)
set_tests_properties(quantapdf.native_clip_states_source PROPERTIES
  TIMEOUT 60 LABELS "conformance;native;no-pdfium;public-api;clip-states")

# Exact Source owners, two-GS identity and allocation rollback.
add_executable(quantapdf_native_clip_states_source_oom
  test_native_clip_states_source_oom.cpp
  "${PROJECT_SOURCE_DIR}/src/backend/qpdf_native_image_ir.cpp"
  "${PROJECT_SOURCE_DIR}/src/ir/ir.c"
  "${PROJECT_SOURCE_DIR}/src/resource/resource_registry.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_ir_v1.c"
  $<TARGET_OBJECTS:quantapdf_native_raster_objects>
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_plan.c")
target_compile_definitions(quantapdf_native_clip_states_source_oom PRIVATE
  QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST=1)
target_include_directories(quantapdf_native_clip_states_source_oom PRIVATE
  "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
target_compile_features(quantapdf_native_clip_states_source_oom PRIVATE
  c_std_11 cxx_std_20)
target_link_libraries(quantapdf_native_clip_states_source_oom PRIVATE
  quantapdf_backend QuantaPDF::QuantaPDF qpdf::libqpdf)
if(UNIX)
  target_link_libraries(quantapdf_native_clip_states_source_oom PRIVATE m)
endif()
if(MSVC)
  target_compile_options(quantapdf_native_clip_states_source_oom PRIVATE
    /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_clip_states_source_oom PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_clip_states_source_oom
  COMMAND quantapdf_native_clip_states_source_oom)
set_tests_properties(quantapdf.native_clip_states_source_oom PROPERTIES
  TIMEOUT 60 LABELS "conformance;native;no-pdfium;source;clip-states;oom")

# Compound Clip winding stays inside the same bounded C11 sampler.
quantapdf_native_test(quantapdf_native_compound_clip_device
  native/test_native_compound_clip_device.c
  "${PROJECT_SOURCE_DIR}/src/render/native_device_ir_v1.c"
  $<TARGET_OBJECTS:quantapdf_native_raster_objects>
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_plan.c")

# Original two-contour W/W* paths, holes and q/Q restoration.
add_executable(quantapdf_native_compound_clip_source
  test_native_compound_clip_source.cpp)
target_compile_features(quantapdf_native_compound_clip_source PRIVATE cxx_std_20)
target_link_libraries(quantapdf_native_compound_clip_source PRIVATE
  QuantaPDF::QuantaPDF qpdf::libqpdf)
if(MSVC)
  target_compile_options(quantapdf_native_compound_clip_source PRIVATE
    /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_compound_clip_source PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_compound_clip_source
  COMMAND quantapdf_native_compound_clip_source)
set_tests_properties(quantapdf.native_compound_clip_source PROPERTIES
  TIMEOUT 60 LABELS "conformance;native;no-pdfium;public-api;compound-clip")

# Exact compound Clip owners and complete allocation rollback.
add_executable(quantapdf_native_compound_clip_source_oom
  test_native_compound_clip_source_oom.cpp
  "${PROJECT_SOURCE_DIR}/src/backend/qpdf_native_image_ir.cpp"
  "${PROJECT_SOURCE_DIR}/src/ir/ir.c"
  "${PROJECT_SOURCE_DIR}/src/resource/resource_registry.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_ir_v1.c"
  $<TARGET_OBJECTS:quantapdf_native_raster_objects>
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c"
  "${PROJECT_SOURCE_DIR}/src/render/native_device_plan.c")
target_compile_definitions(quantapdf_native_compound_clip_source_oom PRIVATE
  QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST=1)
target_include_directories(quantapdf_native_compound_clip_source_oom PRIVATE
  "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
target_compile_features(quantapdf_native_compound_clip_source_oom PRIVATE
  c_std_11 cxx_std_20)
target_link_libraries(quantapdf_native_compound_clip_source_oom PRIVATE
  quantapdf_backend QuantaPDF::QuantaPDF qpdf::libqpdf)
if(UNIX)
  target_link_libraries(quantapdf_native_compound_clip_source_oom PRIVATE m)
endif()
if(MSVC)
  target_compile_options(quantapdf_native_compound_clip_source_oom PRIVATE
    /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf_native_compound_clip_source_oom PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME quantapdf.native_compound_clip_source_oom
  COMMAND quantapdf_native_compound_clip_source_oom)
set_tests_properties(quantapdf.native_compound_clip_source_oom PROPERTIES
  TIMEOUT 60 LABELS "conformance;native;no-pdfium;source;compound-clip;oom")
