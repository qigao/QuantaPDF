cmake_minimum_required(VERSION 3.20)
project(QuantaPDFInstalledC11Consumer LANGUAGES C CXX)

# This is a separate installed-package consumer; there are no in-tree
# include paths, object libraries, internal headers, or direct qpdf calls.
find_package(QuantaPDF 2.34 CONFIG REQUIRED)

add_executable(quantapdf_installed_c11_consumer main.c)
target_compile_features(quantapdf_installed_c11_consumer PRIVATE c_std_11)
target_link_libraries(quantapdf_installed_c11_consumer
  PRIVATE QuantaPDF::QuantaPDF)
# Static Linux/macOS packages need a C++ linker for the QPDF private
# closure, but this application has only one C11 translation unit.
set_target_properties(quantapdf_installed_c11_consumer PROPERTIES
  LINKER_LANGUAGE CXX)
if(MSVC)
  target_compile_options(quantapdf_installed_c11_consumer PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_installed_c11_consumer PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME quantapdf.installed_c11_native_render
  COMMAND quantapdf_installed_c11_consumer)
set_tests_properties(quantapdf.installed_c11_native_render
  PROPERTIES TIMEOUT 30)
