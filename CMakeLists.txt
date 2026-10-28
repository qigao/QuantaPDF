cmake_minimum_required(VERSION 3.20)
project(QuantaPDF VERSION 2.34.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(QUANTAPDF_BUILD_TESTS "Build QuantaPDF tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(QuantaPDFExports)

find_package(qpdf 12.4.0 EXACT CONFIG REQUIRED)
if(NOT TARGET qpdf::libqpdf)
  message(FATAL_ERROR "qpdf did not provide qpdf::libqpdf")
endif()
find_package(ZLIB REQUIRED)
find_package(JPEG REQUIRED)
# qpdf 12.4 exports its static dependencies as bare library names. Normalize
# them to package targets so consumers do not depend on ambient linker paths.
set_property(TARGET qpdf::libqpdf PROPERTY INTERFACE_LINK_LIBRARIES
  "ZLIB::ZLIB;JPEG::JPEG")
set(QUANTAPDF_QPDF_COPYRIGHT "${qpdf_DIR}/copyright")
if(NOT EXISTS "${QUANTAPDF_QPDF_COPYRIGHT}")
  message(FATAL_ERROR
    "qpdf package copyright notice is missing: ${QUANTAPDF_QPDF_COPYRIGHT}")
endif()
get_filename_component(
  QUANTAPDF_VCPKG_SHARE_ROOT "${qpdf_DIR}" DIRECTORY)
set(QUANTAPDF_LIBJPEG_TURBO_COPYRIGHT
  "${QUANTAPDF_VCPKG_SHARE_ROOT}/libjpeg-turbo/copyright")
if(NOT EXISTS "${QUANTAPDF_LIBJPEG_TURBO_COPYRIGHT}")
  message(FATAL_ERROR
    "libjpeg-turbo package copyright notice is missing: "
    "${QUANTAPDF_LIBJPEG_TURBO_COPYRIGHT}")
endif()

# Canonical private C11 Source mixed raster objects: compiled ONCE and
# embedded in the product library or standalone original-source tests.
# No qpdf/PDFium headers or runtime links, no installed/exported target.
add_library(quantapdf_native_raster_objects OBJECT
  "${CMAKE_CURRENT_SOURCE_DIR}/src/render/ir_raster_scoped_mixed_v5.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/render/ir_raster_image_clip_v1.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/render/ir_raster_image_v1.c")
target_compile_features(quantapdf_native_raster_objects PRIVATE c_std_11)
target_include_directories(quantapdf_native_raster_objects PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(quantapdf_native_raster_objects PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(quantapdf_native_raster_objects PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_native_raster_objects PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()

# Canonical provider-free C11 RGB output converter; reuse the exact
# compiled object in the product, the standalone converter test and the
# original-source oracle. No QPDF/PDFium library is linked to this target.
add_library(quantapdf_native_rgb_objects OBJECT
  "${CMAKE_CURRENT_SOURCE_DIR}/src/render/native_backend_rgb_v1.c")
target_compile_features(quantapdf_native_rgb_objects PRIVATE c_std_11)
target_include_directories(quantapdf_native_rgb_objects PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(quantapdf_native_rgb_objects PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(quantapdf_native_rgb_objects PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_native_rgb_objects PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()

# Native Source->IR->Raster and Page adapter belong to the product,
# NOT the backend static archive: product -> backend is the only link
# dependency. Reuse identical private OBJECT files in in-tree oracles
# without exporting new SDK symbols or duplicating C11 render sources.
add_library(quantapdf_native_bridge_objects OBJECT
  "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/native_source_raster_bridge_v1.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/render/native_device_ir_v1.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/builtin_native_page_v1.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/builtin_native_bitmap_v1.c")
target_compile_features(quantapdf_native_bridge_objects PRIVATE c_std_11)
target_include_directories(quantapdf_native_bridge_objects PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(quantapdf_native_bridge_objects PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(quantapdf_native_bridge_objects PRIVATE /W4 /WX)
else()
  target_compile_options(quantapdf_native_bridge_objects PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()

# Two PRIVATE product C11 owner implementation units are also embedded in
# the in-tree original-PDF oracle. A Windows shared DLL must not export
# private facade/Context lifecycle functions just to test them; the exact
# same PIC OBJECTs are linked into product and test, once per executable.
# No additional installed/EXPORT target or reverse backend archive edge.
add_library(quantapdf_context_lifetime_objects OBJECT
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/context_lifetime.c")
add_library(quantapdf_native_facade_objects OBJECT
  "${CMAKE_CURRENT_SOURCE_DIR}/src/document/private_native_facade_bitmap_v1.c")
foreach(_owner_target IN ITEMS
    quantapdf_context_lifetime_objects quantapdf_native_facade_objects)
  target_compile_features(${_owner_target} PRIVATE c_std_11)
  target_include_directories(${_owner_target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
  set_target_properties(${_owner_target} PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
  if(QUANTAPDF_BUILD_TESTS)
    target_compile_definitions(${_owner_target}
      PRIVATE QUANTAPDF_TESTING=1)
  endif()
  if(MSVC)
    target_compile_options(${_owner_target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${_owner_target} PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
endforeach()

add_library(quantapdf_backend STATIC)
add_subdirectory(src/backend)
target_compile_features(quantapdf_backend PRIVATE c_std_11 cxx_std_20)
target_include_directories(quantapdf_backend PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_backend PRIVATE
  qpdf::libqpdf
  JPEG::JPEG
  ZLIB::ZLIB)
if(WIN32)
  target_link_libraries(quantapdf_backend PRIVATE bcrypt)
elseif(APPLE)
  target_link_libraries(quantapdf_backend PRIVATE "-framework Security")
endif()
set_target_properties(quantapdf_backend PROPERTIES
  EXPORT_NAME _backend
  POSITION_INDEPENDENT_CODE ON)

add_library(quantapdf)
# The canonical qualified native Raster objects are the ONLY production
# page-pixel implementation. Unsupported Source input fails closed.
target_sources(quantapdf PRIVATE
  $<TARGET_OBJECTS:quantapdf_native_raster_objects>
  $<TARGET_OBJECTS:quantapdf_native_bridge_objects>
  $<TARGET_OBJECTS:quantapdf_native_rgb_objects>
  $<TARGET_OBJECTS:quantapdf_context_lifetime_objects>
  $<TARGET_OBJECTS:quantapdf_native_facade_objects>)
add_subdirectory(src/core)
add_subdirectory(src/document)
add_subdirectory(src/snapshot)
add_subdirectory(src/composer)
add_subdirectory(src/ir)
add_subdirectory(src/resource)
add_subdirectory(src/validation)
add_subdirectory(src/graph)
add_subdirectory(src/render)
add_library(QuantaPDF::QuantaPDF ALIAS quantapdf)
target_compile_features(quantapdf
  PUBLIC c_std_11
  PRIVATE cxx_std_20)
target_include_directories(quantapdf PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(quantapdf PRIVATE
  quantapdf_backend)
set_target_properties(quantapdf PROPERTIES
  EXPORT_NAME QuantaPDF
  LINKER_LANGUAGE CXX
  VERSION "${PROJECT_VERSION}"
  SOVERSION "${PROJECT_VERSION_MAJOR}")
quantapdf_configure_shared_exports(
  quantapdf "${CMAKE_CURRENT_SOURCE_DIR}/abi/quantapdf-v2.exports")

if(BUILD_SHARED_LIBS)
  target_compile_definitions(quantapdf
    PRIVATE QUANTAPDF_BUILDING_LIBRARY
    PUBLIC QUANTAPDF_SHARED)
endif()

if(MSVC)
  target_compile_options(quantapdf PRIVATE /W4 /WX)
  target_compile_options(quantapdf_backend PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(quantapdf PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(quantapdf_backend PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()

install(TARGETS quantapdf EXPORT QuantaPDFTargets
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/quantapdf"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

# A static SDK must carry the private archive and describe its link closure.
# Shared SDK consumers only link QuantaPDF; its backend is already linked in.
if(NOT BUILD_SHARED_LIBS)
  install(TARGETS quantapdf_backend EXPORT QuantaPDFTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()

if(APPLE)
  set_property(TARGET quantapdf PROPERTY INSTALL_RPATH "@loader_path")
elseif(UNIX)
  set_property(TARGET quantapdf PROPERTY INSTALL_RPATH "$ORIGIN")
endif()

set(_quantapdf_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/QuantaPDF")
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuantaPDFConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/QuantaPDFConfig.cmake"
  INSTALL_DESTINATION "${_quantapdf_package_dir}"
  PATH_VARS CMAKE_INSTALL_LIBDIR)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/QuantaPDFConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion)
install(EXPORT QuantaPDFTargets
  FILE QuantaPDFTargets.cmake
  NAMESPACE QuantaPDF::
  DESTINATION "${_quantapdf_package_dir}")
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/QuantaPDFConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/QuantaPDFConfigVersion.cmake"
  DESTINATION "${_quantapdf_package_dir}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/native-sdk.md"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/quantapdf/docs")
install(FILES "${QUANTAPDF_QPDF_COPYRIGHT}"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/quantapdf/licenses/qpdf")
install(FILES "${QUANTAPDF_LIBJPEG_TURBO_COPYRIGHT}"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/quantapdf/licenses/libjpeg-turbo")
install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
  "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY.md"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/quantapdf")

# The native-only suite has no PDFium input, rendering or oracle. Legacy
# reference-comparison fixtures are not compiled or executed.
if(QUANTAPDF_BUILD_TESTS)
  target_compile_definitions(quantapdf PRIVATE QUANTAPDF_TESTING=1)
  include(CTest)
  add_subdirectory(tests/native)
endif()
