# Native IR Clip V0 — integer rectangle Clip Path

Tracks [#214](https://github.com/qigao/QuantaPDF/issues/214), as the
first strict Clip capability under PDFium-free roadmap #208.

## Exactly admitted

One selected page, one opaque PATH fill, one graphics-state resource with
Normal blend and full alpha, and one path Clip resource with four corners of
an integer-aligned, axis-aligned rectangle, explicit CLOSE and canonical
identity transform. No members, intersection DAG, multiple operations or
other registered resources.

The renderer borrows the original `quantapdf_ir`, validates the clipping
resource and graphics-state IDs, then creates a local no-resource projection
of the fill operation for `quantapdf_ir_raster_render_v1`. It restores the
page background/transparent pixels outside the rectangular mask. This exact
post-mask is valid **only** because one painted operation and integer clip
boundaries are required; general clips will need proper per-sample geometric
intersection, not this shortcut.

Output owns RGBA bytes, the input remains untouched, and all unsupported
semantics fail before allocating/rendering the projected image.
The existing public API, writer, PDFium-backed render and ABI are unchanged.

## Explicit unsupported

Multiple operations or clip nodes, clips with arbitrary geometry, rotated
or fractional coordinates, noncanonical transforms, Clip intersections,
nested q/Q, soft masks, opacity/blend, stroke or other operation kinds.
Do not report support merely because the original PDF renderer could handle
these features.

## Conformance

`quantapdf.native_ir_clip_rect_v0` is a standalone C11 test with no
PDFium/qpdf linkage. It checks exact clipped versus background pixels,
transparent-page behavior, deterministic repeated render, rejection of
invalid shape, noncanonical transform, non-Normal graphics state and bounded
work exhaustion, plus reset on failure.

The broader #214 acceptance remains Open until arbitrary Clip Path winding,
nested intersection resources, definition-time CTM, per-sample antialiasing,
state restoration and independent test-only PDFium pixel comparisons pass
on Linux/ASan+UBSan, macOS, Windows.
