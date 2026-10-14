# #269 — True original jsPDF post-mode7 Stroke/corner/fill residual audit

**Draft / DO NOT MERGE. Strictly test-only read-only measurement.**
Stacked exactly on accepted [#294](https://github.com/qigao/QuantaPDF/pull/294)
HEAD `6794258556b7c7d598a2911964f7afb7439a998e`,
[full CI #38487746137](https://github.com/qigao/QuantaPDF/actions/runs/38487746137)
SUCCESS 3/3 (Linux Release+ASan/UBSan, macOS, Windows).

The externally published, immutable original jsPDF of exactly 3,344 bytes
(SHA256 `179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e`)
has 596x842 PDFium source pixels. Strict private C11 mode5 Stroke and
independently admitted source geometry Fill-right + W Clip-left preview
currently have 2,519 whole-original summed RGB absolute error,
516 differing pixels, zero gross >64, max channel delta 38 and
white RGB FNV64 `37927e64c5916882`. **This is not pixel parity.**

## New measurement only, no renderer change

The pre-existing independent original-PDF A/B CTest performs one extra
bounded, READ-ONLY scan over the already rendered/owned mode5 Stroke RGBA,
actual trial W-clipped red Fill RGBA, final ordered native RGBA and
original, unchanged source-page PDFium RGB. No new render call, generated
PDF, source metadata reinterpretation or compatibility fallback.

Every source-page pixel is assigned to exactly one of sixteen exclusive
source-anchored, actual-C11-layer-alpha guarded regions: bottom-left and
bottom-right single-pass Stroke endpoints, bottom horizontal interior,
top repeated Stroke left/right endpoints and interior, right vertical
Stroke, left vertical below red, left red W Clip/black Stroke overlap,
left red W Clip only, red Fill right edge, red top-right coupled corner,
red top Stroke overlap, red Fill bottom, uncovered bottom red fringe,
and all remaining page pixels. The source-specific X/Y windows are
**reporting categories only and are never production rendering rules**.

Each category reports all scanned/changed/gross counts, signed red/gray
brighter/darker RGB residuals, total absolute channel error, maximum
single-channel mismatch and worst device coordinate. An independent
bounded top-16 list preserves native/PDFium RGB and separate actual
Stroke/Fill alpha. Sixteen fixed source witnesses include bottom Stroke
endpoints, the already exact right vertical/Fill right boundaries,
left W Clip-over-Stroke pixel, top repeat corners and red bottom fringe.

The CMake guard demands 16-region reconstruction of ALL 501,832
pixels and exactly the original whole-page RGB error/count/max, checks
signed error partition, top-worst ordering, original source bytes and
witness geometry and output identity, and explicitly leaves original
whole-page native pixel-positive = 0 / parity=false.

Use the measured residual ranking to choose the next separately
falsifiable *private* source-domain experiment. In particular,
bottom endpoint/cap geometry and the unpainted y=100 Fill fringe
must not be silently mixed into the same new C11 rule. Keep #269 Open,
this PR and #294 Draft / DO NOT MERGE, installed/public SDK PDFium
and #208 backend cutover blocked.
