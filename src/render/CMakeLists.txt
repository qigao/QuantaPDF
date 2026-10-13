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


# Native butt-cap stroke V0 is a private C11 geometry producer over raster V1.
target_sources(quantapdf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_stroke_v0.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_stroke_v0.h")


# Strict native Clip rectangle V0 as a private Raster V1 consumer.
target_sources(quantapdf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_clip_v0.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_clip_v0.h")


# Bounded flat Clip intersection V1: private C11 resource projection only.
target_sources(quantapdf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_clip_intersection_v1.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_clip_intersection_v1.h")


# Native arbitrary single-path Clip uses shared V1 subpixel winding; no PDFium.
target_sources(quantapdf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_clip_path_v2.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_clip_path_v2.h")


# Scoped multi-operation native raster, private C11 and provider-free.
target_sources(quantapdf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_scoped_operations_v0.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_scoped_operations_v0.h")


# Private native nested Clip DAG: bounded C11 sample intersections.
target_sources(quantapdf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_clip_dag_v3.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_clip_dag_v3.h")


# Scoped native operation rendering with reusable bounded nested Clip DAGs.
target_sources(quantapdf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_scoped_operations_v1.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/ir_scoped_operations_v1.h")

# Private bounded C11 page-to-device grid. No raster admission yet.
target_sources(quantapdf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/native_device_grid_v1.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/native_device_grid_v1.h")
