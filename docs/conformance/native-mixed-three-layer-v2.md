# Native Path + two Image Paint V2 — bounded source-order profile

Tracks [#154](https://github.com/qigao/QuantaPDF/issues/154),
[#145](https://github.com/qigao/QuantaPDF/issues/145),
[#427](https://github.com/qigao/QuantaPDF/issues/427) and the full
PDFium-free public-render exit gate [#208](https://github.com/qigao/QuantaPDF/issues/208).

## Additional private C11 painted capability

After the accepted one-PATH/one-Image mixed V1 profile (#432/#434),
`quantapdf_ir_raster_mixed_path_two_images_v2()` accepts **exactly three
operations**, consisting of **one opaque, solid-filled PATH** and **two
separately owned normalized RAW Image resources**, in any of the three
source-order positions. It reuses the existing pure C11 PATH and RAW
image rasterizers and straight-alpha source-over composition. There is
no PDFium, qpdf or PDF export/round-trip in the mixed renderer.

- Each IMAGE is referenced by a unique in-range ID. Unused or aliased
  source resources, extra operations, unrelated graphics states, Clip,
  Form/Font/Paint/SoftMask/Links/Outlines or unsupported Path paint refuse.
- The two Image operations cumulatively charge normalized source pixels
  and destination drawn-pixel work; overlapping image coverage is not
  considered free. The single PATH has the existing checked edges and
  subpixel budget. The output page has the existing one-megapixel hard
  cap and no production backend dependency.
- Every layer must render successfully before the final result is
  allocated/published. Late errors and missing resources reset all
  output fields. The output is uniquely owned and can outlive its source
  IR; every error disposes all already-created temporary layer images.
- The earlier V1 entry point still admits only exactly two layers
  (one PATH + one IMAGE). Image-only and Path-only profiles retain their
  own unchanged limits.

## Original PDF Source → Owned IR → Native

The private `quantapdf_qpdf_source_mixed_path_two_images_to_ir_v2()`
uses the same complete, bounded original-byte QPDF operator parser as
the existing image-only and mixed V1 paths. It allows **one PATH paint
and two named Image XObject Do paints**, in any sequence, under the
already qualified q/Q, source CTM, positive axis-aligned integer
bounds, resource ancestry and decoder limits.

Each Image Do is normalized into a **distinct owned Composer RAW image
resource**; the single PATH's authored source geometry is copied into
an owned PATH operation. Image IDs preserve their original use order,
while the complete three-operation list preserves the original PDF
paint order, even across Content Streams. Source/qpdf lifetime ends
before C11 raster. No source operator is silently omitted.

Text, Stroke, Clip, Form, alpha graphics state, overprint, unsupported
color/CS, masks and other unadmitted page semantics remain exact
fail-closed refusals. The mixed V1 source entry point still rejects
a legal third Image Do rather than acquiring V2 behavior implicitly.

## Qualification and limitations

The PDFium- and qpdf-free C11 test target verifies three-layer pixel
goldens, overlapping alpha, path-above/below-image order, transparent
background, independent repeated results, cumulative source/draw
budgets, malformed/duplicate/missing resources, late unsupported
operation rejection and post-error recovery.

The separate original-source test uses QPDF to author **fixed generated
source bytes**, and renders those **unchanged original bytes directly**
with PDFium as the independent test-only oracle. It tests each PATH
placement in the three source-order positions, split Content Streams,
native RGB parity with fixed interior colors and deterministic repeat
after Source and IR disposal. These generated cases are not new
independently licensed external originals.

No source Text/Font raster, Form XObject rendering, general Image CTM,
nontrivial Source/Clip/Stroke + Image, non-Normal blend, interactive
annotations or public page-render dispatcher is implemented by this
increment. Production PDFium link/install/copy/package dependencies
remain until #208 is fully qualified. This is a narrowly admitted
private Render Profile V2, not arbitrary PDF page support.
