# Native Source Scoped Mixed V8 — normalized nonstroking DeviceGray

Tracks [#427](https://github.com/qigao/QuantaPDF/issues/427),
[#145](https://github.com/qigao/QuantaPDF/issues/145),
[#214](https://github.com/qigao/QuantaPDF/issues/214) and
[#208](https://github.com/qigao/QuantaPDF/issues/208).

## Background and private admission boundary

Merged Source V7 introduced finite normalized nonstroking `rg` path
fill channels in [0,1], while retaining the exact 2..4 mixed
Path/Image layer cap, 1..2 paints of either kind, one frozen `W/W*`
Clip leaf and at least one later paint after a **real Q** Clip pop.
The corresponding C11 raster is still the previously merged
`quantapdf_ir_raster_scoped_mixed_v5()`, with no changes to it.

The new **separate opt-in** private `quantapdf_qpdf_source_scoped_mixed_v8_to_ir()`
accepts the same V7 source profiles plus exactly one numeric operand
for PDF nonstroking DeviceGray `g`, with finite channel `0 <= g <= 1`.
It quantizes to `floor(g * 255.0 + 0.5)`, duplicates that component into
opaque `0xFFGGGGGG` RGB8 Path color, and stages detached owned IR before
letting the unchanged C11 Raster paint. There is **no** C11 format
extension, alpha/blend/provider path or PDFium fallback.

- `g` is restricted to one finite scalar in [0,1]; negatives and
  values above one return `QUANTAPDF_ERROR_UNSUPPORTED`, rather than
  silently clamping. Wrong operand cardinality is `QUANTAPDF_ERROR_FORMAT`.
- Existing V7 fractional `rg` continues to work and must paint
  **byte-identically** in V7 and V8; V1..V7 intentionally still
  **reject** source `g` (including g=0 or g=1).
- All source `q/Q` color-stack and W/W* Clip definition/painting CTM
  semantics, complete Contents stream parsing, resource validation,
  source/paint work caps and atomic no-output rejection remain unchanged.
- Stroking `G`, `RG`, CMYK, ICC/calibrated and alternate ColorSpace,
  external DefaultRGB/DefaultGray redefinitions, transparency,
  patterns, shadings, Text/Font, Form, Stroke and nested Clip DAGs
  remain unqualified and must not be ignored or guessed.

## Independent source PDF qualification

A test-only QPDF author constructs unchanged *original PDF* sources
with the real `g` operand and Image XObject `Do`. The original bytes
are rendered separately using **test-only PDFium**. The Native path is
Source -> uniquely owned detached IR -> already qualified backend-free
C11 Raster, with source/QPDF owners destroyed before Native raster.

The separate `quantapdf.native_source_scoped_mixed_v8_gray` CTest
covers:
1. Two/three/four ordered mixed Path+Image draws, clipped and
   Q-restored, verifying 64, 128 and 223 gray interior goldens.
2. Direct original-source PDFium whole-page RGB tolerance, independent
   color/white goldens and two Native renders byte-identical after
   source and owned IR destruction.
3. `W*` with different definition-time CTM and `n` in the second
   Contents stream, plus nested `q/Q` restoring the prior black color.
4. Strict V7/V6 refusal for new g-positive source and V7/V8 byte
   equality for the already qualified fractional rg PDF.
5. Fail-closed poisoned output and repeated refusal for bad channels,
   malformed arity, stroking G, late Text, a third Image Do, missing
   resources/mask, operation and decoded image-pixel quota exhaustion.
6. Successful original-source rendering after all refusals.

## Acceptance and non-goals

Keep Draft until **exact-head full CI** Linux Release+ASan/UBSan,
macOS, Windows, independent original-PDF pixel/golden checks,
installed SDK and strict licensed external-source corpus reconciliation
all pass. This is only a new private source grammar slice. No public
C11 ABI, production page renderer/thumbnail, shipped PDFium runtime or
package/install change is authorized.

Formal independent external source corpus remains **7 originals /
2 `W*` Native pixel positives**; generated V8 PDFs are not new
licensed originals. Keep #427, #145, #154, #214, #208 OPEN until the
remaining source/render/Clip/Font/transparency and PDFium cutover gates
qualify separately.
