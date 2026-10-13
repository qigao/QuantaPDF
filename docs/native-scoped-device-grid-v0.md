# #269 — Explicit fractional Scoped Raster v0 (synthetic only)

This stacked test/implementation change builds on #275's private, bounded C11
NativeDeviceGrid. It **does not change** the existing
`quantapdf_ir_raster_scoped_operations_v1` or any PDFium/public SDK path.

## Functional scope

The new private `quantapdf_ir_raster_scoped_device_grid_v0` API accepts a
one-page borrowed Detached IR and independently supplied double displayed
geometry. It invokes #275's bounded grid, validates the IR's float page
extents against that geometry, then creates a **stack-scoped borrowed view**
of the page, up to six operations and up to nine Clip nodes. The original IR
and all owned buffers/command arrays remain untouched.

The stage deliberately admits only full-opacity, fill-only Path operations,
identity or zero-as-default Path transforms and identity Clip-leaf transforms,
with finite Path/Clip command coordinates within the normalized display page.
Stroke, general source affine transforms, nonzero origin, intrinsic/requested
rotation, UserUnit != 1, other resources and unsupported Clip DAG structures
are refused with zero RGBA publication. The downstream V1 C11 engine still
validates states, Clip DAG shape/depth/cycles, edge flattening, sample work
and other content; no PDFium or QPDF fallback is available.

Every Path and Clip PATH leaf receives the same nonunit X/Y grid transform;
Clip INTERSECTION nodes keep their identity transform and member IDs. The
temporary page has integer device dimensions, and the existing pure C11
4x4 Path/Clip sampler rasterizes that view. Only final image pixels are
uniquely allocated and owned by the caller. A mismatch with planned output
dimensions forces disposal and zero-output failure. No raw source persistence,
DSO provider dependency or second ownership system.

## Test and claims

`quantapdf.native_scoped_device_grid` is a standalone strict C11 synthetic
fractional-geometry test without PDFium/qpdf. It exercises a 10.25x8.5 point
page projected to 11x9 device pixels, an actual nested Clip intersection
(two leaves), red clipped fill, later blue unclipped fill, fractional-edge
coverage, and byte-identical rerender. It also checks that original input
page, operations and Clip resources remain byte-identical, and that the
preexisting integer-only entry point still returns UNSUPPORTED for the
fractional page.

Negative cases cover stroke, nonidentity Path/Clip transform, UserUnit,
inconsistent source geometry, insufficient sample work, insufficient RGBA
bytes and invalid fill. Every failure must leave no published pixels.

This is **a synthetic native raster functional positive**, not an admitted
independently published original PDF or a PDFium pixel-parity result.
The unchanged original jsPDF remains typed=lowered / native UNSUPPORTED /
zero RGBA under #270. Existing W* original-positive and formal six-source
corpus totals are unchanged. Full C11 Raster/PDFium antialiasing differences,
source precision of individual commands, requested rotation and fractional
stroke scaling remain open in #269. Public SDK stays PDFium and #208 remains
blocked. Keep all staged PRs Draft / DO NOT MERGE.

## Source translation-qualified Fill + W Clip (next stacked stage)

The private fill-only projection now accepts **bounded translation-only**
Path and Clip PATH leaf transforms, matching the source-origin `(x,-y)`
basis and compensating `+page_height` path/clip translations from the
same Source→Graph pipeline. It keeps Clip INTERSECTION ordering and
borrowed Source/IR ownership unchanged, still rejecting scaling, shear,
rotation, Stroke, arbitrary resource types and unsupported page geometry.

The strict C11 synthetic regression requires byte-identical RGBA when
both filled Paths and nested W Clip leaves are represented either in
already-normalized coordinates or with local Y-rebased commands plus
the compensating translation. The independent original jsPDF source's
second (Fill-only) Detached IR operation is also projected into a
one-operation, borrowed Clip-respecting native layer. Fixed red interior
and white exterior PDFium anchors, zero outside native alpha and repeated
byte-identical RGBA are required.

These are **isolated source Stroke and isolated source Fill** candidates,
NOT the correctly ordered composited full-page original and NOT a full
PDFium/native image pixel-parity certification. #270/#277 old and
whole-original opt-in negative gates remain unchanged, as do W* count,
formal external corpus, and public SDK PDFium backend.
