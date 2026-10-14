# #269 — Post-upper-only Stroke residuals on genuine original jsPDF

**Draft / DO NOT MERGE.** Strictly read-only original PDFium-source
diagnostic, stacked on qualified [#298](https://github.com/qigao/QuantaPDF/pull/298)
HEAD `dee2911d297a5d242f0ca674e22d66507b9f31a6`
([full CI #38495275105](https://github.com/qigao/QuantaPDF/actions/runs/38495275105)
SUCCESS 3/3: Linux Release+ASan/UBSan, macOS, Windows).

This branch adds **no C11 production or private raster code**, no new
resource owner, no PDFium fallback, no original source rewrite, SDK
backend migration or pixel-positive claim.

The immutable 3,344-byte published jsPDF source SHA-256
`179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e`
is independently rendered by original-source PDFium at 596×842 pixels.
Accepted private source mode7 **upper-side-only single-pass 1pt Stroke
LSB + mode8 clipped Fill-bottom** has summed RGB absolute error
**1,349**, **223** mismatching original pixels, **0** gross >64, max
channel delta38 and FNV64 `04d883c87f2ec8bc`. This is **not**
pixel parity. The original full-document C11 entrypoint still rejects
UNSUPPORTED/zero output and the official native-positive count is zero.

The prior [#295](https://github.com/qigao/QuantaPDF/pull/295)
read-only source report refers to the *older* mode5+mode7 Fill
state with 516 mismatching pixels. This PR adds an independent
*post-#298* bounded, read-only pass over the same already-rendered
Stroke RGBA, same eighth private clipped Fill RGBA, their
source-ordered composite and untouched original PDFium RGB; no
extra render invocation is needed.

All 501,832 source pixels belong to **exactly one** of 18 source-
specific, actual-layer-alpha guarded diagnostic categories: bottom
single-pass LEFT and RIGHT Stroke endpoints, distinct upper and lower
horizontal interiors, top repeated Stroke left/right endpoints and
interior, right and left vertical source Stroke, red W Clip left
Stroke overlap and red Clip-only, red Fill right boundary,
red top-right/top-left coupled corners, red top Stroke overlap,
red Fill bottom full edge and newly admitted faint bottom fringe,
and exhaustive OTHER. These source-specific coordinates are
**test-only labels, never general native renderer rules**.

For each category report scanned/changed/gross pixel counts, signed
C11-brighter/darker RGB error, total RGB absolute error, maximum delta,
and worst pixel coordinates. Emit **top 16 actual original-PDFium
mismatching pixels**, and **16 source-anchored witnesses** with both
native/PDFium RGB and individual true source Stroke/Fill alphas.
Independent CMake checks reconstruct 501,832 source pixels,
223 mismatching pixels and the entire 1,349 RGB error from the
18 categories; check signed balance, worst severity ordering,
witness positions, RGB/alpha validity, source SHA256, exact
`04d883c87f2ec8bc` previous FNV64, immutable previous negative
global CEIL/round-to-nearest experiments and official native-positive=0.

The observed residual ranking will decide **one** next falsifiable
private C11 hypothesis (likely lower/source Stroke endpoint Miter
coverage), and avoid extending single-pass interior coverage rules
to endpoints, joins, repeated top strokes or Fill/Clip corners.

Keep #269 OPEN and this/upstream stacked PRs **Draft / DO NOT MERGE**.
Installed/public SDK continues to use PDFium and #208 backend switch
remains blocked pending true whole-original pixel parity.
