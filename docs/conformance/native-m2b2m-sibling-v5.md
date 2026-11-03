# M2b.2m: private sibling Text/Fill/Image V5 compilation

The shared mixed-text compiler now binds each operation through its original
GS/Clip ID. The strict Source graph `[1,1,2]` uses Clip1 for Text/Fill and
independent Clip2 for Image. The same typed operation compiler prepares all
three geometry slots in one V5 owner and ledger. No extra parser, raster,
scope-level quota reset or post-render mask is introduced.

The common implementation admits exactly zero, one or two qualified leaves
for its existing unclipped, single-root and new sibling wrappers. Sibling
validation checks both immutable opaque GS records and axis-aligned rectangle
leaves before the first geometry allocation. Projection still uses physical
DPI/72. Text receives the original leaf and physical plan; Fill/Image receive
the corresponding projected leaf. The existing Image and Fill sample shares
remain 1/8 each, with the remainder for Text, under the same 8M/512 page caps.
Old entry predicates, allocation order and goldens remain unchanged.

Original RAW/Flate fixtures 113/114 retain their swapped W/W* leaf rules. The
existing test-only original-quadratic glyph masks plus analytic rectangle and
Image sampling compare every RGBA byte at 72/96/144 DPI. Both leaves must
have exclusive Image sample regions. Negative controls using Clip1 for Image
or manufacturing Clip1∩Clip2 must change visible output at every DPI; the
separate correlated glyph/Clip alpha-product control remains active.

The existing Source/OOM executable tests both originals' every reached C11
allocation at 96 DPI, zero output and released live bytes/reservations, exact
same-IR retry, and unchanged Source payload snapshots. Source pixels, full
projected Image placement, output pixels, operations and peak bytes have
N/N−1 checks. Sample/edge admission thresholds are measured and checked at
N/N−1 under unchanged child shares, separately from billed work. An exact
single-placement cap permits the first execution and atomically refuses the
second; the first output survives compiled-owner disposal. Wrong operation
bindings, aliased GS roots, transformed/skew/cubic/compound leaves and reverse
order refuse before any geometry allocation.

This is PRIVATE compiled-page qualification. Public prepared metadata and
Source→Context→Bitmap sibling rendering remain refused by the existing tests.
No new public ABI, numerical policy, test target, CMake/workflow, timeout or
golden asset. Candidate acceptance requires exact HEAD CI and independent
master CI after merge. #584/#580/#570/#208 remain open; the next gate is public
preparation with Source ownership, receipts, faults and Bitmap lifetime.
