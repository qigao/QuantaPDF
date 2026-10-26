# Native Source Scoped Mixed V7 — finite normalized DeviceRGB

Tracks [#427](https://github.com/qigao/QuantaPDF/issues/427),
[#145](https://github.com/qigao/QuantaPDF/issues/145),
[#214](https://github.com/qigao/QuantaPDF/issues/214) and
the PDFium-removal exit gate [#208](https://github.com/qigao/QuantaPDF/issues/208).

## Why a separate opt-in profile?

Merged source V6 admits 2–4 source-ordered opaque PATH/Image paints,
1–2 of each, exactly one frozen W or W* Clip, at least one clipped paint
and at least one paint after a real Q restores the unmasked graphics
state. The V6 source parser deliberately only accepted nonstroking `rg`
components exactly equal to 0 or 1. Source V7 preserves that already
qualified V6 admission, all existing budgets and the **same** C11
`quantapdf_ir_raster_scoped_mixed_v5()` consumer but independently
qualifies intermediate `/DeviceRGB` PATH fill colors.

## Exact color contract

- Accept only three explicitly present, finite numeric operands for
  nonstroking `rg`. Require **0 <= channel <= 1** for each.
- Quantize each component to `floor(channel * 255.0 + 0.5)` into an
  opaque 8-bit `0xFFRRGGBB` value. No implicit clamping of negative or
  out-of-range channels, automatic alpha, ICC or alternate colorspaces.
- Preserve the parser's existing `q/Q` nonstroking color stack; a color
  set inside one scope must not escape a genuine Q restoration.
- The original V1–V6 source entry points continue requiring exact
  binary 0/1 RGB channels. V7 is a **new private entry point**,
  `quantapdf_qpdf_source_scoped_mixed_v7_to_ir()`, not a public ABI
  addition or a change to their admitted grammar.
- Reject `g/G`, stroking `RG`, `k/K`, `cs/CS/sc/scn`, external
  `DefaultRGB`, Text, Stroke, transparency, Form and other unqualified
  operators/resources atomically. The Path/Image operation limits,
  Clip DAG leaf limit, image/decode and work budgets are unchanged.
- All source/QPDF owners expire before detached uniquely owned IR
  reaches the preexisting pure C11 raster. No PDFium fallback.

## Conformance scope

`quantapdf.native_source_scoped_mixed_v7_rgb` calls the test-only
original-PDF source interpreter on deterministic QPDF-authored PDFs
and paints **the unchanged original source** via independent test-only
PDFium. It checks:

1. Two-, three-, four-paint original-source clipped/Q-restored interleavings
   with quarter, half, three-quarter and other fractional RGB values.
2. Independent whole-page RGB comparison and interior pixel probes for
   64/128/191 and 223/32/96 quantized channels; exact repeated native
   bytes after destruction of source/IR owners.
3. `W*` with Clip definition-time translation and `n` across distinct
   Contents streams, plus `q/Q` color restoration.
4. V6 refusing the same fractional-positive source atomically, and V6/V7
   yielding byte-identical raster for existing exact 0/1 source colors.
5. Rejection and zero published output for negative and over-one channels,
   late unsupported operators, external image mask/missing resources and
   actual operation or image-source-pixel budget exhaustion.
6. Valid rendering after the earlier rejection sequence.

Gate: exact-head full Linux Release + ASan/UBSan, macOS, Windows, static
and installed SDK tests, strict formal externally licensed corpus
reconciliation. Until those gates pass, this is Draft-only.

## Deliberate non-goals

No new Raster path, general ICC/CMYK/CalRGB conversion, source text/font,
Pattern/Gradient, Form transparency group, Blend/soft mask, extended Clip
semantics or public render-device switching. The production page renderer
and thumbnail remain PDFium-backed, and PDFium import/link/package removal
remains unqualified. The formal external source corpus is still **7
independently licensed original PDFs / 2 W* positive Native pixels**
(generated V7 source fixtures do not increase that count). Keep #427,
#145, #154, #214 and #208 open.
