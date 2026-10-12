# Pure C rules live in the existing private archive; they have no provider calls.
target_sources(quantapdf_backend PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/validation.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/graph_validation.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/validation.h")
