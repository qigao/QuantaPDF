#269: explicitly-invoked, STRICT C11 visible-IR Path+W Clip device rendering.
# No PDFium, QPDF, quantapdf_backend, public SDK or fallback.
add_executable(quantapdf_test_native_visible_page_device_preview_v0
  test_native_visible_page_device_preview_v0.c
  "${PROJECT_SOURCE_DIR}/src/render/ir_visible_page_device_preview_v0.c"
  "${PROJECT_SOURCE_DIR}/src/graph/graph_page_device.c"
  "${PROJECT_SOURCE_DIR}/src/graph/graph_page_device_sample.c"
  "${PROJECT_SOURCE_DIR}/src/graph/graph_page_display.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_scoped_operations_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_clip_dag_v3.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster_v1.c"
  "${PROJECT_SOURCE_DIR}/src/render/ir_raster.c")
target_include_directories(quantapdf_test_native_visible_page_device_preview_v0 PRIVATE
  "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
set_target_properties(quantapdf_test_native_visible_page_device_preview_v0
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
  target_compile_options(quantapdf_test_native_visible_page_device_preview_v0
    PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_test_native_visible_page_device_preview_v0
    PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(UNIX)
  target_link_libraries(quantapdf_test_native_visible_page_device_preview_v0 PRIVATE m)
endif()
add_test(NAME quantapdf.native_visible_page_device_preview
  COMMAND quantapdf_test_native_visible_page_device_preview_v0)
set_tests_properties(quantapdf.native_visible_page_device_preview
  PROPERTIES TIMEOUT 30
  LABELS "conformance;native-raster;visible-device-positive-synthetic")
