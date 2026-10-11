# Provider-independent C identity rules share the existing private archive.
target_sources(quantapdf_backend PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/identity.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/identity.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/graph.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/graph_query.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/graph_page_geometry.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/graph_page_geometry.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/graph_query.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/graph_edit.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/graph_write.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/graph_stream.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/graph.h")
