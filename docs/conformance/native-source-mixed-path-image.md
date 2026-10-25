# Original source mixed Path and Image Do — bounded C11 acceptance

Tracks [#433](https://github.com/qigao/QuantaPDF/issues/433),
[#145](https://github.com/qigao/QuantaPDF/issues/145),
[#154](https://github.com/qigao/QuantaPDF/issues/154) and
[#208](https://github.com/qigao/QuantaPDF/issues/208).

## Scope

The private `quantapdf_qpdf_source_mixed_path_image_to_ir()` reuses
**the same strict original-source qpdf Image Do decoder, resource graph
validation and complete PDF content parser** already introduced in
[#429](https://github.com/qigao/QuantaPDF/pull/429) and hardened in
[#431](https://github.com/qigao/QuantaPDF/pull/431). It admits one
original PDF page only if **every** source operator is understood, then
produces independently owned Composer PATH + IMAGE IR in original paint
order. The Source/QPDF owner is destroyed before rendering. The borrowed,
backend-free, pure-C11 `quantapdf_ir_raster_mixed_path_image_v1()` (merged
[#432](https://github.com/qigao/QuantaPDF/pull/432)) draws the result.

**No PDF is re-exported** as an intermediate rasterization. Test-only
PDFium reads the original, unchanged authored source bytes independently.

This first, deliberately small profile accepts:

- Identity-oriented 72-DPI page with integer [0,0,W,H] visible MediaBox
  and CropBox, no annotation or group; bounded /Contents streams and
  inherited /Resources resolved using existing PageTree limits.
- Balanced q/Q, positive axis-aligned cm and named /Image Do with the
  **unchanged exact decoder and effective color/visibility restrictions**
  from #429/#431 (8-bit DeviceGray or DeviceRGB, RAW/Flate, no masks,
  optional content or interpolation).
- Exactly **one opaque solid fill PATH** and **one IMAGE** in either
  source order. Admitted Path constructor grammar: a single m/l/h
  polygon or re rectangle, followed by f or f*. Nonstroking rg is
  admitted for **exact channel 0 or 1 only**; default color is opaque
  black. Vertices are transformed under the currently active CTM at
  construction time into integer, page-contained, top-left positions.
  No intermediate q/Q or cm is admitted with an unfinished path.
- Checked, uniquely owned path commands and Image RAW pixels; no
  source memory, provider state, font or file dependency remains.

There is **no silent partial page rendering**. Stroke, Clip W/W*,
forms, text, soft masks, color space changes, graphics-state gs,
marked content, arbitrary CMYK, gradients, inline images, extra
paint operations, malformed Resource/PageTree graphs, fractional/sheared
or out-of-bounds positions, and unknown operators return explicit
FORMAT or UNSUPPORTED with **zero output fields**. Hard and caller
page/source/image/draw/stream/operator budgets are preserved; the
existing mixed pure-C11 raster also independently preflights pixel,
path edge and sample quotas.

## Evidence

The independent source-oracle CTest uses QPDF **only to author** original
source fixtures. It renders those unchanged PDF source bytes via direct
test-only PDFium, and compares them to the detached pure-C11 Native mixed
page RGBA output. It covers source PATH then IMAGE and IMAGE then PATH,
rectangle/polyline/f* variants, cross-Content-stream Do and q/Q, path
CTM, fixed interior colors and full-page pixel differences, byte-exact
repeat and owner detachment. Rejected content after valid Path/Image
paints must never leak partial IR or pixels, and successful subsequent
source parsing must still produce the same output.

This does NOT qualify Image alpha/group/SMask source semantics, combined
Clip resources, fonts, glyph outlines, text placement or general mixed
PDF page compatibility. Those remain explicit unqualified feature
families under #145/#154/#213/#214/#427/#433.

## Production cutover

The public `quantapdf_render_page()` and thumbnail dispatch continue to
use the PDFium backend. The CMake installed SDK still links and packages
PDFium. This private original-source subset is **not** a public rendering
fallback or a production cutover. It does not admit additional independent
licensed external PDFs; formal original corpus remains **7 / 2**, and
the generated source family manifest remains separately accounted.

The next increment should expand operation composition or source coverage
without ever accepting partial pages. Final deletion of PDFium runtime,
link, installation and package paths remains the #208 exit gate.
