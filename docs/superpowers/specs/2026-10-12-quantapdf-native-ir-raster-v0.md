# Native IR Raster V0 — strict, backend-free consumer

Tracks [#209](https://github.com/qigao/QuantaPDF/issues/209) under
[#208](https://github.com/qigao/QuantaPDF/issues/208); initial CPU milestone
for [#145](https://github.com/qigao/QuantaPDF/issues/145) and
[#154](https://github.com/qigao/QuantaPDF/issues/154).

## Intent and ownership

\`src/render/ir_raster.h/.c\` provides a **private** C11 consumer of const
\`quantapdf_ir\`. It never links PDFium/qpdf, calls the Composer PDF writer,
or resolves backend objects. The output is a detached, malloc-owned RGBA8
bitmap, top-left origin, straight alpha, contiguous rows, released with
\`quantapdf_ir_raster_image_dispose\`.

There is no public SDK entry point, provider fallback, persistence format or
new versioned ABI. The current public \`quantapdf_render_page\` continues using
PDFium until future cutover gates pass.

## Exactly supported V0 language

- Integer-sized pages and explicit per-call pixel/operation limits.
- Background is fully opaque ARGB, or transparent when suppressed.
- Filled, solid, opaque, axis-aligned rectangles at integer coordinates:
  \`MOVE, LINE, LINE, LINE, CLOSE\`; any winding orientation.
- Fixed RGBA color, exact pixel coverage, operation-order overlap.
- Zero or identity path transform; no graphics state/resources.

Everything else fails closed as **UNSUPPORTED**, including strokes, cubics,
noninteger geometry, clips, transforms, gradients, alpha/blend, text, images,
glyph runs, forms, tiling, shaders, navigation, resource-backed effects and
unsupported background alpha. Invalid page indices/limits/structural pointers
return **ARGUMENT**. The render preflights all selected-page operations before
allocating any bytes. All failures reset the result slot to empty.

The V0 scope is intentionally much smaller than a PDF content/graphics
renderer. It must not be described as supporting general shapes or PDFs.

## Product layering and gates

- Production compiles the source into the private QuantaPDF core target.
- \`quantapdf_test_native_ir_raster\` compiles **only two C11 files** (test and
  raster) with private IR data declarations, and does not link QuantaPDF,
  PDFium, qpdf or other backends. It is labeled \`native-raster;conformance\`.
- Golden pixels test page background, transparent background, two overlapping
  shapes, deterministic repeated output, and detached result lifetime.
- Negative tests cover unsupported stroke, alpha, nonidentity transform,
  curve, fractional point, nonempty resource registry, absent budgets,
  operation counts and invalid selected page.
- Existing full Linux Release/ASan/UBSan and Windows/macOS builds must remain
  green. Do not merge the draft without test evidence for its exact HEAD.

## Further increments before PDFium removal

1. General path fill with cubic flattening, antialiasing and even-odd/nonzero.
2. Stroke with cap/join/miter/dash and exact transform semantics.
3. Clip intersections, groups, opacity, blend modes and soft masks.
4. Gradient/pattern/image decoding, nested Form content, text/Glyph Run raster.
5. PDF parser/Content IR expansion and independent extraction.
6. Corpus pixel comparison against PDFium kept as a **test-only reference**,
   installed SDK tests without a PDFium runtime, dependency and license audit.

Every new feature must extend *both* admission and rendering with explicit
resource budgets and failure atomicity; no approximate success statuses.
