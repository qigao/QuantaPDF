target_sources(quantapdf_backend PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/builtin_backend.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/qpdf_document.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/qpdf_graph.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/qpdf_composer.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/composer_text_layout.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/ttf_font.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/secure_random.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/jpeg_validator.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/jpeg_encoder.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/pdfium_document.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/pdfium_runtime.cpp")
