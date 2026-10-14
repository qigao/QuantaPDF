# #269 — Mode7 private source upper-side horizontal Stroke Alpha A/B

**Draft / DO NOT MERGE.** Independently measured on the untouched
original externally published jsPDF source, SHA256
`179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e`,
exactly 3344 bytes, original MediaBox 595.28x841.89 points.
The raw original page is rendered directly by PDFium at 596x842 device
pixels, **501,832 original device pixels and 1,505,496 RGB channels**;
no generated PDF is used to fake source equivalence.

## Source-domain evidence and the negative previous candidate

[#296](https://github.com/qigao/QuantaPDF/pull/296), exact HEAD
`40c390c16be4d2f9062fd91de2c462da9270442b`, full
[CI #38491406146](https://github.com/qigao/QuantaPDF/actions/runs/38491406146)
SUCCESS Linux Release+ASan/UBSan/Windows/macOS, qualified the existing
explicit private mode5 Stroke plus private right-Fill, left-nonzero
W Clip, bottom-Fill source rectangle previews at **1,937** summed
RGB absolute error, **419** mismatched source pixels, zero >64 gross,
max channel delta38, output white RGB FNV64 `7a88c595fcde2208`.
Still not pixel parity, still not native-original admission.

The independent private [#297](https://github.com/qigao/QuantaPDF/pull/297)
sixth Stroke candidate tested global CEIL rather than nearest-integer
alpha for all admissible single-pass horizontal interior pixels. Full
raw-original macOS/Windows source comparisons measured
**2,525 absolute RGB error, 615 changed pixels** and FNV64
`d2d04c526c2c20dc`: a regression. The diagnostic
pixel (100,139) retains the SAME integer alpha under both ceil and
round, while 196 pixels on the opposite horizontal boundary gain one
alpha and become wrong. **CEIL globally is rejected as a model**, not
hidden by a false positive score gate.

## Seventh explicit private C11 Stroke entrypoint — new hypothesis

`quantapdf_ir_raster_device_polyline_stroke_upper_horizontal_lsb_preview_v0`
keeps the exact mode5 source eligibility: width1pt axis horizontal
source segment, geometrically single traversal, exclusion of all
overlapping source edges, at least a source radius and a full
device pixel away from both ends, and no diagonal acute miters
or self-crossing. All other source segments — repeated upper black
Stroke, verticals, caps/joins, wider strokes — reuse byte-identical
mode5 behavior.

The NEW condition is **source geometric** and distinguishes the
two sides of the same horizontal source line. ONLY if the entire
device row is above the source horizontal segment center
(`pixel_y1 <= source_segment_y`), the rounded mode5 source Alpha
is nonsaturated and nonzero, mode7 adds **ONE** Alpha unit.
The lower/crossing side remains the exact old mode5 nearest-integer
model. This is NOT PDFium internal modeling, a sample shift, source
hardcoding, or a general AA specification.
Bounded work `count*7` is charged before any output allocation;
mode0–6 independent functions/bytes/budgets remain unchanged.
No additional owner, fallback, retry, runtime-state persistence
or public SDK/ABI exposure.

## Gate details

Strict standalone pure-C11 CTest checks exact geometry-derived
72/96/144 DPI upper-device-side alpha, alpha increase at most +1,
RGB byte identity, deterministic re-render, old-mode5 equivalence
on repeated source horizontals, isolated verticals and diagonal
acute-miter geometry. A separate 85,000-sample budget admits old
mode6 (count6*13,200) and refuses new mode7
(count7*13,200) with fully **zero RGBA output**.

The existing independent original-source A/B CTest renders the
new Stroke twice, composites only with the SAME previously
qualified mode8 Fill-bottom source-owned layer in Stroke→Fill
order, compares ALL original PDFium pixels/channels and emits
unrestricted real error/count/gross/max/FNV64, original pixel
(100,139) and separately measured source alpha. The test enforces
all candidate source changes are inside the original true
bottom horizontal top-side interior x52..248,y138..139 **as a
TEST-only isolation window**, changed Alpha at most +1 and
unchanged Stroke RGB bytes. The checker freezes both the accepted
mode8 original 1,937 / 419, digest `7a88c595fcde2208`,
and the independent global CEIL negative 2,525 / 615, digest
`d2d04c526c2c20dc`, plus immutable PDF SHA and all original
negative and official corpus-count gates. It **does not demand**
that the new mode7 improves the score.

#269 remains OPEN; all stacked experimental branches remain
**Draft / DO NOT MERGE**. Original whole-document native is
`UNSUPPORTED` / zero partial output; official native W/W* original
positive count unchanged. Installed/public SDK continues to use
PDFium, and #208 cutover remains blocked.
