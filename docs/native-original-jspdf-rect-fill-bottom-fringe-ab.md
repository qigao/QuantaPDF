# #269 — Original jsPDF clipped Fill bottom weak fringe, private eighth A/B

**Draft / DO NOT MERGE**, stacked exactly on [#295](https://github.com/qigao/QuantaPDF/pull/295)
HEAD `705565df39368d313f50baa635efc3678741d0e5`, qualified by
[full Linux Release+ASan/UBSan, Windows, macOS CI #38489845558](https://github.com/qigao/QuantaPDF/actions/runs/38489845558)
SUCCESS 3/3. No production SDK backend, ABI, original file or source
ownership is modified.

The actual 3,344-byte SHA-pinned upstream jsPDF, SHA256
`179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e`,
is rendered directly by PDFium at 596×842 device pixels. Previous
source-only private mode5 Stroke + mode6 solid red Fill RIGHT boundary
+ mode7 nonzero W Clip LEFT boundary has summed RGB absolute error
2,519, 516 mismatch pixels, zero >64 gross pixels and FNV64
`37927e64c5916882`; this does NOT mean pixel parity.

The independent 16-bin [#295](https://github.com/qigao/QuantaPDF/pull/295)
read-only original source audit identifies the **Fill BOTTOM previously
uncovered, faint red fringe** at display y=100: 98 mismatching pixels,
588 RGB absolute error, max channel delta 3. At source pixel
(100,100), native mode7 displays (255,255,255) while genuine
original PDFium shows (255,252,252). The source PDF explicitly
defines a solid red Fill x=0..150, display y=0..100 bounded by
one nonzero W Clip x=50..250,y=20..140. The fractional original
page 841.89pt high projects to 842 device rows at 72dpi:
source row y=100 begins near 99.987pt, so the Fill bottom covers
~0.013 of that one device pixel (~3/255 alpha). This is a
falsifiable source-geometry observation, NOT a claim of PDFium
internal antialiasing semantics.

## Eighth explicit/private C11 candidate (test-only)

`quantapdf_ir_raster_rect_fill_bottom_preview_v0` is appended to
the existing private C11 rectangle module and **does not modify**
its previously qualified Fill-right and W Clip-left functions.
It strictly reuses source coordinate geometry parsing and independent
Clip/Fill rectangle admission, unchanged source state/transforms,
one borrowed original Detached IR and the single owned mode7 RGBA
output. The additional source bottom rectangle/device row intersection
is evaluated only if fractional, strictly inside the W Clip's top
and bottom, and the old mode7 RGBA pixel is **fully clear**.
It adds red color from the admitted Fill source and bounded
`round(255 * true source-pixel Y overlap)` alpha **only to the
single source-bottom row more than a full pixel clear of both X
corner intersections**. All old Stroke/cap/miter/Clip left/Fill
right pixels and the top Fill overlap remain byte-identical.
No hardcoded source coordinates, PDFium fallback, global phase,
second ownership channel or automatic native backend switch.
Admission pre-charges `fill_edges+clip_edges+3` sampled-edge work
BEFORE output allocation. No weak border or invalid alpha/color
implies transactional UNSUPPORTED with fully zero output.

## Full original and strict C11 gates

Standalone strict C11 synthesis tests verify a fractional Fill
bottom row, analytic alpha, correct newly owned red RGBA, unchanged
previous x-left/x-right borders, deterministic repeats, original
producer-style retraced rectangle top edges, separate mode7-allowed
but mode8-budget-refused transactional zero-output, wrong rule/blend,
nonrectangular source and exact-aligned bottom row rejection.

The independent already existing original jsPDF A/B CTest (NOT
the near-40s whole-original native negative) renders mode8 Fill
**twice**, composites each with the already accepted mode5 Stroke
in the correct original source order, and independently compares
all 501,832 original PDFium pixels / 1,505,496 RGB channels.
It measures total/mean RGB error, changed/gross/max and FNV64,
validates full-page repeat and restricts all changed source RGBA
pixels to original y=100, x=52..148 **in the test only**.
The CMake checker preserves all prior mode4–mode7 original byte
digests and exact mode7 2,519/516/0/38 baseline, plus source SHA,
and deliberately imposes **NO mandatory improvement** by mode8.

Public SDK continues using PDFium, original whole-document C11
remains UNSUPPORTED/zero pixels, official W/W* positive count
unchanged and #208 cutover blocked. #269 stays Open, this and
upstream Draft PRs **DO NOT MERGE** pending exact-head CI.
