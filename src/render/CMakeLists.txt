# Pure raster planning is shared with providers through the private archive.
target_sources(quantapdf_backend PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/render_plan.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/render_plan.h")

# Native render consumer: pure C11, no PDFium/qpdf symbols, private ABI only.
target_sources(quantapdf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_raster.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_raster.h")

# Standalone native IR general-path consumer; no PDFium/qpdf calls or headers.
target_sources(quantapdf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_raster_v1.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_raster_v1.h")
