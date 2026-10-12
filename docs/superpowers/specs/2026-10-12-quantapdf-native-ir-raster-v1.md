# Native IR Raster V1 — general path fills

Implements the private PDFium-free general-path milestone tracked by
[#211](https://github.com/qigao/QuantaPDF/issues/211), beneath the
[#208](https://github.com/qigao/QuantaPDF/issues/208) removal roadmap.
The public C11 ABI and PDFium-backed `quantapdf_render_page` are unchanged.

## Supported rendering

- Detached RGBA8, top-left pixel origin, one integer-sized IR page.
- Owned IR `PATH` operations with solid, fully opaque fill; path syntax is
  MOVE / LINE / CUBIC / CLOSE, multiple subpaths and implicit closure.
- Even-odd and nonzero winding with half-open Y edge admission.
- Path affine transform before tessellation; zero transform means Composer
  identity. Nonfinite or singular transforms and unrepresentable geometry are
  rejected.
- CUBIC subdivision with a 1/8 pixel flatness tolerance and maximum
  subdivision depth 12. No indefinite subdivision or unbounded recursion.
- 4×4 fixed, deterministic pixel coverage with fixed center-of-cell sample
  locations; RGBA8 straight-alpha source-over uses integer rounding. This is
  **not** a claim of pixel identity with the PDFium rasterizer.
- Opaque page background or fully transparent page canvas, in IR operation
  order. Repeated identical inputs produce identical bytes in the same build.

## Strictly unsupported

Stroke, dash, clip paths/intersections, alpha/blend resources, gradients,
tiling patterns, images, text, glyphs, forms, soft masks, document navigation,
unsupported transform values and unsupported commands are rejected before any
pixels are published. Existing V0 rectangle API remains available unchanged.

## Ownership and admission

`quantapdf_ir_raster_render_v1` borrows a const IR for one synchronous call.
Its result buffer is malloc-owned and disposed using
`quantapdf_ir_raster_image_dispose`; no borrowed Graph/Composer/PDFium state
escapes. Failed calls clear the output slot; callers must first dispose an
earlier successful value rather than reusing a live result slot.

Limits specify maximum pixels, operations, flattened edges, and total
per-sample edge intersection tests. Each selected-page operation is preflighted
and all billed work is checked *before* allocating the final pixel buffer.
A cubic that cannot satisfy the stated tolerance within subdivision limits
returns `UNSUPPORTED` rather than being silently coarsened.
Coordinates and affine components are finite, bounded to ±1,000,000.

## Conformance

`quantapdf.native_ir_raster_v1` is built exclusively from C11 sources.
It links no PDFium/qpdf or QuantaPDF runtime. It tests repeatability,
hole/winding behavior, transparent alpha, edge antialiasing, implicit closure,
cubic flattening, affine rotation and skew, plus fail-closed budgets and
rejected semantics. All regular project tests, installed SDK gates and
Linux sanitizers remain mandatory.

This milestone is an independent renderer for a **restricted subset of IR**;
full PDFium-free SDK capability needs stroke/clip/color/image/font/text/form
coverage and document-to-IR interpretation and extraction. No fallback paths.
