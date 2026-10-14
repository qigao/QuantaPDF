# #269 — Raw original jsPDF source-region error budget after upper-side Stroke A/B

**Draft / DO NOT MERGE. Read-only source-PDF residual localization only.**

Parent [Draft #298](https://github.com/qigao/QuantaPDF/pull/298)
exact HEAD `dee2911d297a5d242f0ca674e22d66507b9f31a6` qualified by
[CI #38495275105](https://github.com/qigao/QuantaPDF/actions/runs/38495275105):
Linux Release + ASan/UBSan, Windows and macOS **SUCCESS 3/3**.
This independent branch does not modify C11 rendering, SDK, source
or its previous accepted original source output.

The upstream published 3,344-byte jsPDF original, SHA256
`179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e`,
is evaluated against independently rendered **original-source** PDFium,
not a C11-re-exported PDF, at 596×842 = 501,832 original device pixels
and 1,505,496 RGB channels.

The exact source-ordered mode5 Stroke + admitted private Fill-bottom
mode8 reference had **1,937 RGB absolute error, 419 differing pixels**
and FNV64 `7a88c595fcde2208`. The separately explicit private
upper-side-only one Alpha LSB preview #298 retained the same clipped
red Fill and lowered the full-page source RGB error to **1,349,
223 differing pixels**, zero gross >64, max channel delta38,
FNV64 `04d883c87f2ec8bc`. This is still NOT original-source
native pixel parity; formal positive count stays zero.

## Read-only source-domain audit of BOTH stages

The existing independent original-source A/B CTest already owns
the true original PDFium RGB, old Stroke, accepted upper-side Stroke,
unchanged clipped Fill-bottom RGBA and two ordered full-page C11
composites. This branch reads these existing buffers once more; no
extra PDF export, render, parser, converter, owner, backup, fallback
or public SDK change.

All **501,832 source pixels** are partitioned into exactly 16
mutually exclusive test-only original-jsPDF source-region buckets:
bottom Stroke left endpoint / right endpoint / true single-pass
interior; top repeated Stroke left/right endpoints and interior;
right vertical Stroke and left vertical below red; red nonzero W Clip
left over black Stroke / left only; Fill right edge, coupled red
top-right corner, red top black Stroke, red Fill bottom, uncovered red
bottom fringe, and all other original device pixels.

Each group reports old-vs-PDFium error **and** new-vs-PDFium error,
old and new differing-pixel counts, positive (native brighter)
versus negative RGB channel residuals, gross pixels, maximum channel
delta and its genuine device coordinate. The true top 16 remaining
original-PDFium mismatches report unchanged C11 RGBA layer alpha,
new native RGB and source PDFium RGB. Another 16 pinned source
witnesses include *both* old and new RGB, the old and new Stroke
Alpha, the unchanged red Fill Alpha and the genuine original
PDFium RGB.

CMake independently reconciles **16 bins and 501,832 original
pixels** against the previously measured whole-image totals and
source fingerprint, all 16 witness names/coordinates/RGB range,
the top-worst sorted mismatch severity and signed channel
differences. It also freezes the old (1,937/419) and accepted
upper-side +1 (1,349/223/0/38) source numbers and output FNV64.
There is **no new acceptance threshold** and no new renderer
hypothesis in this PR.

The source X/Y windows are **test-only labels for this immutable
external original jsPDF**, never hardcoded conditions for an actual
C11 PDF renderer. Use the resulting residual decomposition to decide
whether source endpoint/cap/acute-miter geometry merits a **separate
bounded private A/B** or if Fill/Clip corners or old 4×4 rounding
are a higher-confidence next domain. Never extend the already
qualified mode9 single-pass upper-side alpha rule to endpoints
based on this report alone.

Keep #269 OPEN, #298 and this PR **Draft / DO NOT MERGE**.
Public/installed SDK continues using PDFium, full original native
C11 stays UNSUPPORTED with zero partial output, formal external
W/W* source-positive unchanged and #208 backend cutover blocked.
