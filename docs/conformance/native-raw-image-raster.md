# Native RAW Image Raster V1 — private C11 admission

Tracks [#154](https://github.com/qigao/QuantaPDF/issues/154),
[#145](https://github.com/qigao/QuantaPDF/issues/145) and the complete
PDFium removal gate [#208](https://github.com/qigao/QuantaPDF/issues/208).

## What this actually implements

`quantapdf_ir_raster_raw_images_v1()` draws normalized, **owned
Composer IR image resources**, producing detached straight RGBA8 pixels
in top-left row-major order without using PDFium, qpdf, a provider,
a decoder or an external renderer.

The first admitted profile is intentionally small:

- One integer-dimension page, **1–4 IMAGE operations**, in stored source
  order. Every registered image resource must be referenced by at least
  one operation. No unrelated font, Form, paint, Clip or GS resources.
- **RAW Gray8 or RGB24** resource planes stored exactly as the Composer
  resource registry normalizes them (tight rows), with an optional
  separate 8-bit alpha plane. Reject JPEG/PNG-encoded image resources,
  non-normalized buffers, invalid dimensions and mismatched planes.
- Rectangular, page-contained, integer-coordinate **STRETCH** placements
  only, with deterministic nearest-center source pixel sampling.
  Opaque background or explicit transparent background are supported.
- **Straight-alpha source-over** across overlapping image operations,
  preserving correct output alpha and color through an integer, portable
  64-bit compositing calculation.
- Explicit upper bounds: maximum 4 operations, 4,194,304 output pixels,
  4,194,304 cumulative source pixels and 16,777,216 cumulative painted
  destination pixels, further restricted by the caller's budgets.
  Repainting overlapping pixels is charged again.

The preflight checks every referenced source image, placement, operation,
resource association, page background and all four budgets **before
allocating even one output pixel**. Unsupported constructs return
`QUANTAPDF_ERROR_UNSUPPORTED` with a completely zeroed output. Invalid
API arguments return `QUANTAPDF_ERROR_ARGUMENT`. The IR is borrowed only
while rendering; the caller owns the successful output and releases it
with `quantapdf_ir_raster_image_dispose()`.

The C11 test target `quantapdf.native_ir_raw_images_v1` deliberately
links **no PDFium or qpdf**. It checks Gray/RGB raster sampling,
multiple layers, an independent alpha plane, opaque/transparent
backgrounds, exact repeat images, detached result ownership, and
malformed source/late-operation/budget rejection.

## Original Image XObject Source bridge (qualified separately under #427)

The private qpdf structure provider can now construct an **owned RAW IMAGE
Composer IR** from complete original PDF page content, using the strict
`quantapdf_qpdf_source_image_ir()` admission gate. Unlike ordinary image
occurrence extraction, the source bridge refuses **every other page
content operator**: a page containing paths, text, other graphics-state
operations, unsupported images or Forms cannot be silently displayed as
a partial image-only result.

The initial original-source admission is limited to identity page rotation
and UserUnit, a supported origin-aligned page box, bounded inherited
XObject resources, and a content program containing only balanced `q/Q`,
finite `cm` and named Image `Do`. Image XObjects must be 8-bit
DeviceGray or DeviceRGB without masks/alpha/Decode overrides and with
uncompressed or explicitly lossless Flate data. The caller explicitly
limits operations, source pixels, page pixels and PageTree ancestry.
Axis-aligned positive-scale **integer STRETCH** placements are admitted;
shears, rotations, fractional bounds, unsupported filters, annotations,
transparent page Groups, inline Images and Forms fail atomically.

Test-only direct PDFium reads the **unchanged original source** independently
and compares interior Gray/RGB color orientation and whole-page pixels with
the detached C11 image result. The qpdf provider is destroyed before
Native painting. Negative tests cover late unsupported operators,
missing XObjects, unsupported page paint, resource/operation/work budgets,
bad transforms and corrupted image masks. This is *not* an extension of
the formal licensed external-original corpus: generated QPDF fixture
documents remain test-only, and the accepted published corpus count is
unchanged.

The bridge is a qpdf-backed **structure/decode** adapter, not a qpdf raster
backend and not an implicit fallback; the lower-level RAW image raster
still builds and executes without qpdf or PDFium linkage. It is not wired
into `quantapdf_render_page()`.

## Critical scope boundary

This is the first **owned-IR image-paint primitive**, not evidence that
arbitrary PDF source `Do` Image XObjects, inline images, `SMask` trees
or decoded source image resources already flow into the renderable IR.
Existing **native image observation** and bitmap extraction live on a
separate path and must be connected to typed image operations with
owned lifetime, source CTMs, resource resolution and further color/mask
conformance under #145.

This raster does not mix images with Path/Clip, render Form XObjects,
implement arbitrary image transforms or `CONTAIN/COVER` sizing, or
approximate unsupported features. Those are explicit follow-ups.
The production `quantapdf_render_page()` and thumbnail path still
call the PDFium backend; QuantaPDF SDK still imports, links and packages
PDFium. **#208 remains OPEN.**

## Next step

Wire one originally authored, byte-pinned source Image XObject into
the detached Composer IR, retaining the existing no-PDFium C11 image
paint result and comparing against **original-PDFium as a test-only
independent oracle**. Then expand mixed Path/Image/Clip painting under
one bounded all-or-nothing operation plan, avoiding either re-export to
PDF or a PDFium fallback.
