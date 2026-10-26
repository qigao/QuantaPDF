# Original PDF: W/W* clipped Image Do, Q-restored opaque Path V4

Tracks [#427](https://github.com/qigao/QuantaPDF/issues/427),
[#214](https://github.com/qigao/QuantaPDF/issues/214),
[#145](https://github.com/qigao/QuantaPDF/issues/145),
[#154](https://github.com/qigao/QuantaPDF/issues/154) and the
production PDFium exit gate [#208](https://github.com/qigao/QuantaPDF/issues/208).

## Strict original-source admission

The new private
`quantapdf_qpdf_source_clipped_image_then_path_to_ir_v4()`
admission profile accepts exactly **one W or W* frozen Clip PATH**,
followed by **one named Image XObject Do while the Clip is active**,
then a real `Q` that restores the previous clip-free graphics state,
then **one opaque PATH Fill** after the old Clip is removed.

The same complete original PDF source parser handles all operators,
including ordered Content Streams and operators split across boundaries.
Allowed drawing-path commands are `m/l/h/re` and an exact source
`f/f*`, with source RGB channels constrained to 0 or 1.
Image samples remain strictly referenced owned 8-bit DeviceGray/RGB
RAW or qualified Flate. Images use positive axis-aligned integer
placement; Clip definition CTM is frozen when constructed, not moved
by later Do placement. A restored Path has its independent fill/CTM,
with graphics_state_id zero.

One detached Composer IR owns two source-ordered operations
(IMAGE with GS/Clip ID 1, PATH with GS/Clip ID 0), one RAW image resource
and exactly one frozen Clip leaf with its GS. The QPDF source owner ends
before any C11 Native rendering.

Late Text/Stroke/extra Image/Path, missing named resource,
unsupported Form, image masks/transparency, malformed CTM, scope,
content or resource budget **reject the entire page**, even after a
valid clipped Image has already been staged. No IR or pixel output
is published on failure. Previous V1/V2/V3 clipped Image signatures
retain their narrower semantics and do not admit PATH paint implicitly.

## Native C11 V4 raster

`quantapdf_ir_raster_clipped_image_then_path_v4()` renders exactly
these two already-owned operations through separate provider-free
primitives: the existing RAW+Clip V1 (4×4 sample winding mask) for the
first Image and the existing general opaque PATH V1 **without Clip**
for the restored second layer.

Both layers are rendered on isolated transparent surfaces. The
global work budget for Clip/Path edges and subpixel tests is split
conservatively in half between layers; image source pixels and painted
destination area are admitted separately. The original source order
is preserved by integer straight-alpha source-over. Intermediate
raster memory is owned, cleaned on all failures, and the final
detached RGBA8 buffer is published only after both layers succeed.

`quantapdf.native_ir_raw_image_clip_path_v4` is a standalone C11
CTest without qpdf or PDFium links. The independent original-source
test `quantapdf.native_source_image_clip` authors original PDF bytes
and renders **those unchanged bytes** through test-only direct PDFium
for complete-page RGB comparison and fixed red/green/blue/yellow,
red-outside-Clip and white probes. It exercises W, W* across streams,
Clip-definition CTM distinct from Image CTM, and strict negative/recovery
matrices. The owned source IR and QPDF original are released before
Native Raster bytes are compared; repeat results are byte-identical.

## Not yet complete

This **single clipped Image then restored Path** profile is not
general PDF rendering. It excludes multiple independent Clip roots,
Clip DAGs with mixed Image/Path, image masks, non-integer/negative
transforms, Form, shading, arbitrary source text/font rasterization,
annotations and transparency groups. It does not route public
`quantapdf_render_page()` or thumbnail to Native and does not remove
PDFium from production CMake/SDK packages. Full exit stays open under
#208.

The formally licensed external source count remains **7 original
PDFs / 2 independently authored W* Native pixel positives**. All new
PDFs used here are generated qualification fixtures, not additions
to that count.
