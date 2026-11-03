# M2b.2p: private compound Text/Fill/Image V5 compilation

The shared mixed operation compiler accepts the exact owned Source graph from
M2b.2o: one opaque Normal GS names root3=Intersection(1,2), and ordered
Text→Fill→Image operations name GS1. Both leaves are independent identity-space
rectangular W/W* paths. Root membership, both leaves, GS and operation order
are validated before any geometry allocation. Cyclic/duplicate/missing IDs,
transformed roots/leaves, skew/cubic leaves and reverse order remain refused.

The same typed compiler projects the two original leaves once. The existing
Text geometry compiler snapshots both original leaf commands with the physical
plan; the same 16-bit glyph mask is intersected with each leaf at the same
4×4 subpixels before averaging. Every visited leaf edge is charged before its
predicate. Fill and Image pass both projected leaves into the existing V1
visible Fill/mask compiler. No reduced rectangle, second sampler, post-render
mask multiplication or per-leaf quota reset is introduced. Existing zero/one
leaf Text wrappers and single-root/sibling V5 allocation order remain intact.

One V5 owner/compositor and page ledger retain the 8M sample/512 edge ceilings,
Image/Fill 1/8 sample shares and their existing 16-edge geometry limits. The
remaining Text share includes both leaves. Unique source pixels and full Image
placement are charged once; clipping does not lower the authored placement.

Original RAW/Flate 126/127 retain their existing geometry and compare every
RGBA byte at 72/96/144 DPI. Their Fill is completely outside leaf2. New original
145/146 retain the glyphs, Fill and Image but widen leaf2 leftward, so all three
layers have nonempty sample coverage. Existing original-quadratic per-glyph
sample planes plus analytic independent leaf/Fill/Image tests produce every
reference byte. Each DPI has visible changes when either leaf is omitted,
disjoint/partial glyph intersections, and visible spurious ink from multiplying
separate averaged glyph/leaf1/leaf2 alphas. No golden asset is changed.

Both new originals at 96 DPI fault every reached ledger-backed C11 allocation,
require zero output and released live bytes/reservations, preserve the Source
IR snapshot, and retry exact bytes. Source/output/full-placement pixels,
operations, peak bytes and measured sample/edge admission have N/N−1 checks.
A second execution cannot reset placement charges; the first output survives
failed repetition and compiled-owner disposal. Twelve malformed graph variants
refuse before any geometry allocation. Existing singleton/sibling tests use
the same verification helper and retain their prior coverage.

This is PRIVATE compiled-page qualification. Prepared metadata and actual
public Context→Bitmap calls still refuse all four compound originals at every
supported DPI, followed by exact same-Context recovery on accepted profiles.
Public preparation, receipts, Context fault/recovery and Bitmap lifetime need
their own gate after independent master acceptance. ABI, caps, test projects,
registrations, CI/CMake, timeouts and old goldens remain unchanged.
