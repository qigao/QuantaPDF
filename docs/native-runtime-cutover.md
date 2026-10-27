# Native QuantaPDF runtime — PDFium removed

This repository's production runtime is **PDFium-free**. The public C11
`quantapdf_render_page` and `quantapdf_render_page_with_options` use the
existing **one** Source V10 (QPDF structure/Flate decoding) → detached IR →
pure C11 native raster → owned RGB bitmap pipeline. No provider retry,
reference renderer, PDFium runtime import/library, PDFium package or licensing
install is permitted. All unsupported features return
`QUANTAPDF_ERROR_UNSUPPORTED`, with an empty output handle; they never
partially paint or select a fallback.

## Currently implemented native rendering subset

- One original, unencrypted page at index zero, exact 72 DPI, no rotation,
  region clip or alpha-mode changes; UserUnit 1.
- Public thumbnails now use the SAME fully validated native 72-DPI page
  bitmap and bounded deterministic RGB area downsampling, preserving
  aspect ratio with no upscaling and no second PDF parser/provider. All
  unsupported original content still fails closed before resizing.
- Native blank pages with no painting (missing/empty Contents or
  balanced q/Q and transform-only content) produce owned opaque white
  bitmaps; text, page annotations and unsupported operations never
  become silent blank output.
- One frozen W/W* Clip, correct Q restoring scopes, 2–4 source-ordered
  8-bit Gray/RGB Image and opaque Path Fill operations, strictly bounded
  to at least one operation of each type for painted pages.
- Direct Image XObject RAW/Flate, explicit Decode identity/inversion,
  Predictor 1/2/10..15 with checked Colors/Columns/BitsPerComponent.
- Finite normalized RGB/Gray solid fill colors and exact owned RGB output.
- Unsupported text, annotations drawing, font glyphs, forms, soft masks,
  gradients, advanced strokes, transparency, alternative Image formats,
  general page geometry and unsupported render options fail closed.

## Native feature development and test gate

The native-only suite checks deterministic fixed RGB goldens, exact repeated
bitmaps, source-document ownership teardown, invalid-device and late-source
error reset, pure C11 Clip/Raw Image/scoped mixed Raster and device planning.
There is no reference renderer comparison, downloaded PDFium binary or
differential pixel oracle in the build/CI.

Next native implementation tasks should extend the **source grammar, owned IR,
raster painter and device pipeline** directly, gated by feature-specific
fixed fixtures and negative work/quota/ownership tests. Do not restore a
PDFium fallback to fill any gap.

The public C11 ABI remains unchanged. The SDK is a C ABI backed by C/C++
implementation; qpdf 12.4, ZLIB and JPEG remain private structure/codec
dependencies. There is no installed PDFium import or runtime.
