# Native Clip DAG V3 — bounded nested intersections

Tracks [#214](https://github.com/qigao/QuantaPDF/issues/214) within
[#208](https://github.com/qigao/QuantaPDF/issues/208). Private C11 runtime,
one opaque solid PATH operation, one normal/full-alpha graphics state,
and a Clip DAG with a nested intersection root.

## Semantics

This increment accepts **3–8 total Clip nodes**, composed only of
`CLIP_PATH_INTERNAL` leaves and `CLIP_INTERSECTION_INTERNAL` nodes.
At least one nested intersection is required. Intersections have 2–4
members; maximum traversal depth is 4; **at most four distinct path leaves**
are supported. Shared references (DAG, not just tree) are admitted and
deduplicated. Cycles, duplicated siblings, missing references, unreferenced
nodes, invalid CTMs and unsupported resources fail closed.

All Clip Path leaves keep independent finite nonsingular affine transforms
and Even-Odd/Nonzero fill rules, along with MOVE/LINE/CUBIC/CLOSE and implicit
closing. Each source Clip Path is flattened once into the **same bounded
native raster edge buffer** as the selected painted PATH. At each fixed 4×4
pixel sub-sample, all unique leaf winding predicates must be true before
the pixel receives the solid paint's coverage. Because this is an
intersection-only Clip DAG, flattening the graph into a conjunction of all
reachable unique leaves preserves its Clip semantics. It is not an attempt to
flatten Union/XOR/difference or other nonexistent Clip node kinds.

## Ownership and resource bounds

Immutable `quantapdf_ir` and each leaf's path geometry are borrowed for one
synchronous call; there is no heap-owned Clip DAG copy and no mutation. DFS
marks bound recursion, reject active back-edges and deduplicate shared nodes.
Each render attempt preflights the painted path and **all** reachable Clip
paths under the caller's total operation/pixel/edge/sampled-edge budget,
including adaptive cubic flattening limits, **before allocating output
pixels**. All failure returns clear output and publish no partial bitmap.

The independently allocated RGBA8 result is released by the existing
`quantapdf_ir_raster_image_dispose`. No PDFium/qpdf calls, fallback, state
serialization, retry, public C11 ABI addition or dependency change.

## Regression tests

The provider-free standalone `quantapdf.native_clip_dag_v3` includes:
nested 3-leaf rectangular Clip intersections; deterministic repeated bytes;
fractional affine leaf boundary with partial subpixel coverage; transparent
page; a shared leaf reached by two branches; cyclic/self references,
duplicate members, invalid and missing Clip IDs, singular affine, unsupported
stroke, extra unreachable resources, and edge/sampled-edge budget exhaustion.

The test compiles and links only C11 native renderer sources, without
QuantaPDF/QPDF/PDFium runtime targets.

## Explicitly remaining

The existing QPDF Composer writer supports flat Clip intersections only, so a
**new test-only PDFium nested-DAG oracle cannot honestly use its current
serialization**. Expand the writer's Clip traversal and validation in a
separate task before claiming independent PDFium pixel qualification. Also
unfinished: multiple operations sharing nested Clip graphs, arbitrary q/Q
PDF content-stream state interpretation, Stroke+Clip, transparency, masks,
group blending, fonts, images, extraction and PDFium-free SDK packaging.
#214 and #208 remain OPEN; public `quantapdf_render_page` remains PDFium-backed.
