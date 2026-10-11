target_sources(quantapdf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/barcode.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/composer.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/composer_font.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/composer_internal.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/composer_navigation.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/svg.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../resource/composer_resources.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/../ir/composer_operations.h")
