# #269 — Test-only decimal CropBox rational device-grid A/B

**Private diagnostic; Draft / DO NOT MERGE. No production parser, Graph ABI,
renderer, DPI allowlist, SDK backend, fallback or release changes.**

Parent [Draft #358](https://github.com/qigao/QuantaPDF/pull/358)
was qualified on [CI #38753480692](https://github.com/qigao/QuantaPDF/actions/runs/38753480692):
Linux Release + ASan/UBSan, Windows, macOS all SUCCESS. Four original CI
JSON reports agreed on 24 test-generated Fill-only original PDFs rendered
at 72/96/120/144/168 DPI, including **24 admitted-grid versus PDFium
dimension mismatches**: 12 at 72 DPI, 0 at 96 DPI, 12 at 144 DPI, all
associated with XY+0.375 CropBox. The existing private Grid v0 continues
to reject non-admitted 120/168 DPI with complete zero output.

## Isolated cause

Graph's independent, locale-neutral PDF decimal decoder builds a binary
double by accumulating decimal digit contributions (0.1, 0.01, etc.).
For the test-written original CropBox tokens `[2.375 3.375 12.375
11.375]`, the two endpoints can differ from their exact binary
representations by opposite signs. Subtracting these doubles can produce
a displayed-page width slightly **greater than 70 points** and a height
slightly **greater than 56 points** at leaf `/UserUnit 7`, despite the
**exact original decimal** spans being 10 and 8 PDF units. The Grid
v0 deliberately applies `ceil(width_points × DPI/72)` to determine
bitmap dimensions without an epsilon. Therefore 72/144 DPI expose an
extra device column or row, while 96 DPI can retain the same integer
bitmap size because its nominal extent is already fractional.

## Actual source-backed diagnostic

Reuse the already qualified **test-only QPDF original PDF writer**,
preserving its four decimal CropBox literal variants, inherited/leaf
placement, leaf `/UserUnit 7`, MediaBox, opaque red Fill content stream,
and intrinsic /Rotate=0/90/180/270. Generate **16 real test-owned
original PDF byte streams**, each opened by the regular capped Source→
Graph path. The source closes before Graph geometry interrogation and
Graph snapshots drop before reference PDFium rendering. Re-open each
original for independent PDFium RGB render twice at admitted DPI
72/96/144. Expected dimension budget: 48 observations across
**426,400 reference pixels**, max 140×112 per image, with byte-exact
PDFium repeat and source bytes unchanged.

For each original, compare two **independent, test-only** Grid v0 calls,
both using the *existing* public-private API with its unmodified
allowlist and capacity:

1. **Raw Graph page geometry:** values as actually decoded from
   original PDF page dictionary numbers; preserve every positive/negative
   near-integer size and its actual strict `ceil()` result.
2. **Test-only rational decimal candidate:** the original QPDF-written
   CropBox literals are independently known as the exact integer
   thousandths `[2000,3000,12000,11000]`,
   `[2125,3000,12125,11000]`,
   `[2000,3125,12000,11125]`, and
   `[2375,3375,12375,11375]`. Construct a **copy** of the
   selected page geometry from these source literal values using
   exact integer span differences *before* binary conversion, retaining
   original rotation, UserUnit and MediaBox. Feed only this **copy**
   into the unchanged private Grid v0, never rewriting any original
   file, Graph snapshot, provider state, Composer IR or Raster source.

Report original PDFium raster dimensions, the actual raw-Graph grid,
the isolated rational candidate grid, raw/candidate displayed-page
point extents, per-crop/per-DPI original-PDFium size mismatch counts
and extra-row/extra-column direction separately. A complete
source-backed pixel-dimension match from the rational candidate is
a **test diagnostic**, not authority to update Graph's decimal parser:
it does not independently validate arbitrary decimals, exponent syntax,
sign, overflow, bounded work or compatibility against other PDF producers.

The new separate CTest validates **all 48 frame rows** against original
PDFium sizes, strict source byte and bitmap repeat checks, exact
rational geometry admission, and non-promotion guards; it also requires
**32 additional fail-closed UNSUPPORTED/zero-output** 120/168 DPI
requests. It must not silently reclassify any raw-grid mismatch as
a positive native pixel test. Per-platform original JSONs are
published independently for Linux Release, Linux ASan/UBSan, Windows
and macOS. Even if all four exact-HEAD reports agree, the next
stage should prove a generalized bounded decimal/rational parser
with targeted rejected-number and cross-producer cases before
considering any production code change.

**#269 and #343 remain OPEN. Every PR in this stack remains Draft /
DO NOT MERGE. The independently published reporter Userunit.pdf still
has unresolved original bytes/hash and document-specific redistribution
permission.** The formal third-party native-pixel positive count does
not increase, installed/public SDK remains PDFium and #208 backend
switch is blocked.
