# #269 — Exact original jsPDF post-Fill-right residual localization

**Draft / DO NOT MERGE. Read-only test-only diagnostic.** Stacked on
the independent [#292](https://github.com/qigao/QuantaPDF/pull/292) exact
HEAD `f6a64de9cc5507a8c075710ed7a6c34907b62cf6`, with
full Linux Release+ASan/UBSan, macOS and Windows
[CI #38484942252](https://github.com/qigao/QuantaPDF/actions/runs/38484942252)
SUCCESS 3/3.

The genuine 3,344-byte published jsPDF original, SHA-256
`179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e`,
at 596x842 pixels is source-rendered by PDFium. Previous accepted
private C11 mode5 Stroke, followed by the independently admitted
Fill-right geometric preview, currently has **4,019 summed absolute
RGB error, 516 differing pixels, zero >64 gross pixels, FNV64
`4d2bfdd4bbf32298`**. These are *not* pixel-parity qualifications.

## This branch adds no rendering rule

The pre-existing independent original-PDF A/B CTest scans its already
allocated mode5 Stroke RGBA, trial clipped Fill RGBA, true
source-ordered C11 composite and raw-original PDFium RGB exactly once.
No new source PDF, additional bitmap render, altered fill/stroke path,
SDK/ABI, owner or fallback is introduced.

It assigns **every one of the 501,832 original pixels** to exactly one
of eleven disjoint source-window + actual-C11-alpha categories:
bottom single horizontal Stroke; right vertical Stroke; left vertical
below red; top repeated Stroke; red Fill right boundary; red left
Stroke/Clip overlap; red left Clip only; red top/black-Stroke overlap;
red Fill bottom edge; red bottom uncovered fringe; all other pixels.
Source-specific coordinates are **diagnostic labels only**, never
production heuristics, general Clip logic or PDFium implementation
claims.

For each region report original pixel count, changed/gross pixels,
summed RGB error, signed brighter/darker channel error, max delta and
the position of its worst pixel. Separately emit the true full-page
12 worst mismatches with actual C11/PDFium RGB and both source layer
alphas, plus 12 pinned-source cross-domain witness pixels.

The independent CMake machine checker reconstructs full-page
pixel/change/gross/RGB error and signed balance, checks all reported
source witness coordinates, RGB/alpha bounds, severity ordering, old
Fill-right FNV64 and original source SHA. No improvement threshold or
new native-original positive may be created by this scan.

Keep Issue #269 Open and stacked #291/#292 and this PR Draft /
DO NOT MERGE. The installed/public SDK continues rendering through
PDFium and #208 backend cutover remains blocked. The observed residual
budget determines which **one** next private effect-domain A/B is
justified; do not shift global raster phase or generalize source
coordinate windows.
