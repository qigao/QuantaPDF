target_sources(quantapdf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/bitmap.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/bitmap_internal.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/context.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/context_lifetime.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/context_internal.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/input_file.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/input_file.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/memory.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/output.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/output_lifetime.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/output_file.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/output_internal.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/status.c")
