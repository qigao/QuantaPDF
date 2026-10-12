# Native Clip intersection V1 — flat, bounded rectangles

Tracks [#214](https://github.com/qigao/QuantaPDF/issues/214), under
[#208](https://github.com/qigao/QuantaPDF/issues/208) and #154.

## Supported exactly

One selected page and one opaque PATH operation, with one normal full-alpha
graphics-state resource. Its clip reference must point to a single
`CLIP_INTERSECTION_INTERNAL` root with **2–4 distinct** member IDs. Every
member must refer directly to a `CLIP_PATH_INTERNAL` leaf, containing exactly
one axis-aligned, integer-coordinate, closed rectangle with a canonical
identity transform. All registered clips must be accounted for by this graph.
Nested roots, duplicate/cyclic references and unreferenced resource nodes
fail admission.

Each leaf's rectangle is validated, then intersected with all other leaves.
An empty intersection is currently **UNSUPPORTED**, rather than interpreted
as an error-free transparent paint. The resulting exact integer rectangle is
projected onto a stack-owned canonical single-leaf IR view. Existing V0
performs the final painting using bounded Native Raster V1 and restores the
pre-operation page background outside the mask.

This projection is semantically valid only because there is **one** paint
operation, all Clip leaves have integer edges, and all transforms are
identity. It cannot represent arbitrary rotated/polygonal clips or preserve
intermediate paint outside changing clip states.

## Safety and limits

The immutable original IR is borrowed but not modified or retained.
No PDFium/qpdf calls or indirect fallback; the isolated native C11 test links
only native raster source files. At most four members are inspected; input
clip IDs, dimensions and exact shape grammar are validated before constructing
the projected view. Resource budgets remain enforced by V0/V1; failures
publish a reset output slot.

## Testing

`quantapdf.native_clip_intersection_v1` checks two and three intersected
rectangles, overlap pixels, background preservation and transparent canvas,
deterministic output, IR state unchanged, and rejection of duplicated, zero,
self-referential or nested resource IDs, fractional or noncanonical leaf,
empty intersections, alpha changes, invalid pages and work exhaustion.

Remaining #214 work: arbitrary winding/path clips, fractional edge coverage,
nested intersection graphs, per-operation changing clip state, definition-time
CTM, soft masks and **separate test-only PDFium pixel comparisons**.
The public PDFium-backed renderer and C11 ABI are unchanged.
