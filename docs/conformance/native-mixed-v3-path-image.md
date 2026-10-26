# Native mixed V3: two Path fills and two RAW Images

Tracks [#154](https://github.com/qigao/QuantaPDF/issues/154),
[#145](https://github.com/qigao/QuantaPDF/issues/145),
[#427](https://github.com/qigao/QuantaPDF/issues/427) and the complete
PDFium removal exit gate [#208](https://github.com/qigao/QuantaPDF/issues/208).

## Added owned IR and C11 rendering profile

Two **independently owned, opaque, solid-filled PATH operations** and two
**separately owned RAW Gray/RGB Image XObject paints** can be rendered in
any of the **six source-order interleavings**, without re-exporting the
page to PDF. Both PATH fills retain their own path geometry, exact source
0/1 RGB color and even-odd/nonzero rule. Each image uses the earlier
bounded, top-left integer STRETCH placement and optional owned Alpha
plane. Their straight-alpha outputs are composited in **original source
order** on a single RGB/transparent page background.

- New strict original-source private entry point:
  `quantapdf_qpdf_source_mixed_two_paths_two_images_to_ir_v3()`.
  It reuses the complete-operator qpdf source interpreter with balanced
  q/Q, positive axis-aligned cm and referenced Image Do admission.
  All unsupported operations (including a late text/stroke/Clip),
  malformed image/resource references and exceeded budgets **invalidate
  the entire page**, without partial IR publication. No qpdf objects
  survive successful IR ownership transfer.
- New pure-C11 raster entry point:
  `quantapdf_ir_raster_mixed_two_paths_two_images_v3()`. It reads
  exactly four owned IR operations (two PATH/two IMAGE), rejects all
  unused or extra resources, and renders all four independently in
  one private native RGBA owner. No PDFium or qpdf dependency is linked
  by `quantapdf.native_ir_mixed_two_paths_two_images_v3`.
- Pixel output is bounded by 1,048,576 pixels. The two PATHs each receive
  at most **one half** of the caller's edge/sample budgets, ensuring the
  combined work cannot evade a per-page limit by rasterizing each layer
  separately. Image source pixels and destination repaint pixels remain
  cumulatively charged across both Image operations. Earlier V1 (1P+1I)
  and V2 (1P+2I) admission numbers are unchanged, with no implicit
  behavior expansion.
- No successful bitmap is published until every source operation, layer,
  budget and composited output is completely admitted and rendered.
  Failures clear output dimensions, pixels and size.

## Independent qualification

The source-oracle test authors an **original PDF** (two Image XObject
streams, two differently colored PATH fills), opens the unchanged PDF
through **test-only direct PDFium** and independently compares the
detached Native pure-C11 image with full-page RGB, fixed untouched/red/
blue/green/yellow interior goldens and exact repeated native RGBA.
Six PATH/Image interleavings and different /Contents stream boundaries
are covered. Source PDF input is invalidated before native rendering,
and the detached output remains valid after the IR is destroyed.

Negative tests cover a fifth Image, a third PATH, late text/stroke/Clip,
missing or Form XObject, interpolation, insufficient operation and
source-pixel budget, malformed/duplicate Image IDs, extra resources,
unqualified Path alpha, short Path sample/edge budgets and valid
recovery after repeated fail-closed attempts.

## Still outside the production exit gate

This remains a **tiny private C11/source profile**. It does not implement
general PDF painting, text/font glyph rasterization, Image masks,
non-axis-aligned transforms, Form transparency groups, overprint,
annotations, multi-page dispatch, arbitrary resource graphs or generic
Clip. It is not a public ABI addition and does not cut over
`quantapdf_render_page()`, remove PDFium CMake import/link/install
dependencies or change formal licensed external corpus accounting,
which remains **7 original PDFs / 2 W* pixel-positive originals**.

The next relevant increments should address mixed Path/Image Clip/GS
with full-page ownership and then text/font raster/source-operator
coverage. No successful Native rendered page may be silently assembled
from a supported subset while unsupported source paint is omitted.
