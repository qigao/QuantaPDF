# Native arbitrary Clip Path V2 — shared subpixel geometry

Tracks [#214](https://github.com/qigao/QuantaPDF/issues/214).
This increment is a **single opaque fill operation with one Clip Path
resource**, not a whole-PDF renderer or an arbitrary Clip DAG.

## Design

The native V1 raster engine exposes a second **private C11** entry point:
`quantapdf_ir_raster_render_v1_clipped`, which flattens the painted path and
the clip path into the **same checked edge buffer**, bills both against the
existing pixel, edge, operation and intersection-work budgets, and admits
each paint sample only when its 4×4 subpixel point satisfies **both**
winding predicates. This avoids the common but incorrect approximation of
multiplying two separately averaged antialiasing masks.

`quantapdf_ir_raster_clip_path_v2` validates one normal full-alpha graphics
state, one direct Clip Path ID and no other resources or operations; it
borrows the original immutable IR, strips just the graphics-state references
in a local value view and calls the shared sample renderer synchronously.
Neither source IR nor Clip resource is modified. Result owns detached RGBA8
bytes with exactly-once caller disposal.

Clip grammar: MOVE, LINE, CUBIC, CLOSE with implicit closure and multiple
subpaths, Even-Odd or Nonzero winding, finite nonsingular affine matrix,
including fractional translation and rotation. The same native adaptive
Bezier-flattening depth/tolerance and deterministic 4×4 raster coverage
rules govern paint and clip geometry. Invalid curves/resource references,
work exhaustion and unsupported semantics fail without publishing pixels.

## Gates

`quantapdf.native_clip_path_v2` is a standalone C11 test. It compiles only
the native raster C sources (no QuantaPDF runtime, PDFium or qpdf linkage).
The identical-diagonal test is the key exact-integer regression: intersecting
a path with itself must produce **bit-identical pixels** to un-clipped
rendering, even on partially covered edge pixels. Additional cases test
fractional clip translation, Even-Odd hole, transparent background,
noncanonical/singular transforms, unsupported resources and work budgets.

Current public `quantapdf_render_page` remains PDFium-backed. The earlier
Clip V0 and flat rectangular intersection V1 continue unchanged.

## Explicit remaining scope

No Clip intersection DAG, no multiple painted operations or q/Q state
changes, no soft mask or transparency-group blending, no stroke+clip, and no
full external PDF content interpretation. A separate **test-only** QPDF /
PDFium differential pixel gate will be required before marking this Clip
increment fully qualified. The product code never invokes either backend
as a fallback.
