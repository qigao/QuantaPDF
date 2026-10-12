# Pure raster planning is shared with providers through the private archive.
target_sources(quantapdf_backend PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/render_plan.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/render_plan.h")
