# Native original-source two Image Do under one W/W* Clip — private V2

Tracks [#427](https://github.com/qigao/QuantaPDF/issues/427),
[#214](https://github.com/qigao/QuantaPDF/issues/214),
[#145](https://github.com/qigao/QuantaPDF/issues/145),
[#154](https://github.com/qigao/QuantaPDF/issues/154) and the full PDFium
production cutover [#208](https://github.com/qigao/QuantaPDF/issues/208).

## Exact admitted source contract

The existing strict full-page qpdf source reader gains an **opt-in V2**
`quantapdf_qpdf_source_two_clipped_raw_images_to_ir_v2()` entry point.
It extends, but does not broaden, the older single-Image source API.

One original 72-DPI source page must contain:

- One constructed direct `W` or `W*` Clip Path terminated by `n`.
  `m/l/h/re` are admitted constructors with bounded source coordinate
  transforms. The Clip geometry and fill rule are frozen at definition time.
- **Exactly two Image `Do` paints** in source order, both while the same
  original Clip scope remains active. Balanced `q/Q` may change each
  Image's independently checked placement CTM; saved/restored Clip ownership
  is never borrowed from a discarded scope.
- Strictly referenced 8-bit DeviceGray or DeviceRGB Image XObjects, each
  decoded to its own **uniquely owned** RAW Composer Image resource.
  A repeated Image name still yields two ordered owned resources and two
  operations; none of the source qpdf/Graph owners escape.
- One resulting Graphics State, one Clip Path leaf, two Image operations
  each referencing its own Image resource and the same immutable Clip ID.
  The earlier single-Do API rejects this two-Do source.

A separate pure-C11 consumer
`quantapdf_ir_raster_two_clipped_raw_images_v2()` paints two individually
clipped RAW layers in source order, using the already-qualified V1
RAW+Clip implementation. Image work/source budgets are preflighted in
**aggregate**, and Clip edge and sample budgets are conservatively split
between layers. Each temporary image is fully owned and released before
the final output is published. If the second layer fails, the result
remains entirely zeroed and neither intermediate is published.

## Evidence and limitations

The original-source test builds QPDF-generated source bytes, which are
loaded **unchanged** by test-only PDFium to obtain an independent RGB
reference. The strict Source interpreter consumes the original, then
the original bytes and qpdf reader are discarded before the pure C11
Raster runs. Tests verify exact repeated Native RGBA, fixed RGB
quadrant/overlap/white goldens and whole-page PDFium difference bounds
under W, W* across multiple Content Streams and different Clip/image
definition CTMs. Negative cases include third Image, one Image, popped
Clip before second Image, late unsupported Text/Path/color/unknown
resource, invalid alpha/mask/CMYK, transform errors and second-layer
capacity exhaustion.

A second **provider-free C11 test target** checks source-order alpha,
opaque/transparent page backgrounds, shared Clip/winding, global
source/drawn/sample budgets, malformed later IR and atomic reset. The
C11 target has **no PDFium or qpdf linkage**.

This is **not** multi-Clip DAG state per operation, mixed Image/Path
within a Clip, arbitrary source CMaps/masks/Forms, native font painting
or full public renderer cutover. The public page render and thumbnail
APIs continue using PDFium under #208, until separately qualified.
Externally licensed source corpus remains **7 originals / 2 W* native
pixel-positive originals**; generated source cases do not increase it.
