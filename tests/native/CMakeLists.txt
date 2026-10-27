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
  QuantaPDF::QuantaPDF qpdf::libqpdf)
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
