# Provider-independent C identity rules share the existing private archive.
target_sources(quantapdf_backend PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/identity.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/identity.h")
