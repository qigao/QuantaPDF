# Original Image Do under PDF W/W* Clip — Native private V1

Tracks [#427](https://github.com/qigao/QuantaPDF/issues/427),
[#214](https://github.com/qigao/QuantaPDF/issues/214),
[#145](https://github.com/qigao/QuantaPDF/issues/145),
[#154](https://github.com/qigao/QuantaPDF/issues/154) and the PDFium
production-removal gate [#208](https://github.com/qigao/QuantaPDF/issues/208).

## Actual admitted source-to-native path

A private new `quantapdf_qpdf_source_clipped_raw_image_to_ir_v1()`
uses the **same full-page strict PDF operator parser** already qualified
for original source Image Do and mixed Path/Image painting. It does not
reuse the image occurrence observer (which is allowed to ignore other
painting operators). The original source PDF is read and decoded
completely, including all selected ordered Content Streams.

This is a narrowly bounded PDF subset:

- One original 72-DPI page with admitted zero-origin MediaBox/CropBox,
  identity rotation/UserUnit, no Group or annotations.
- Balanced `q/Q`, positive axis-aligned bounded `cm`, one
  constructed direct `W` or `W*` Clip PATH terminated by `n`,
  and **exactly one referenced Image `Do` painted inside its live Clip
  scope**. Path constructors are `m/l/h/re`; no Path painting is
  admitted in this profile.
- Clip path points are **frozen at construction time** under the
  definition CTM, then mapped into top-left page coordinates. This
  becomes one owned immutable Composer Clip PATH leaf with the
  corresponding nonzero/even-odd rule and identity local transform.
  Subsequent Image CTM changes do not modify the frozen Clip.
- One linked IMAGE operation, owned normalized RAW DeviceGray/RGB 8-bit
  sample buffer, and exactly one immutable Graphics State binding the
  Image to the Clip. All source/qpdf owners have ended when the IR is
  passed to `quantapdf_ir_raster_clipped_raw_image_v1()`.
- The private pure-C11 Image+Clip renderer uses the established
  4×4 Path subpixel mask and a separate bounded RAW Image sampler
  with straight alpha; no qpdf, PDFium, re-exported PDF or provider
  is used to produce Native pixels.

Any other/extra Image, Path/Text paint, Form, Mask, GS, Blend,
shading, graphics resource, unsupported CTM, partial q/Q, unsupported
Clip DAG, missing named Image, unknown PDF operator, invalid
resource hierarchy or budget exhaustion **refuses the entire source
page with zero published IR/bitmap**. No hidden best-effort partial
painting or fallback to PDFium.

## Test evidence

`quantapdf.native_source_image_clip` synthesizes an original PDF with
an actual 2×2 RGB Image XObject. It independently loads those unchanged
bytes with **direct test-only PDFium** and compares the complete RGB page
against the detached Native C11 result at bounded error, with fixed
white/red/green/blue/yellow interior goldens and byte-identical repeat
rendering.

The positive source cases exercise original `W`, `W*` split from
`n` across two Content Streams, and a Clip-definition CTM different
from later Image placement. Original source/qpdf data is released
before Native Raster; the owned IR is disposed before inspecting the
detached image. Negative tests cover unsupported late Text, Path/Color,
pop-before-Do, extra Do, invalid/missing XObject and mask/color,
invalid CTM or fractional Image placement, malformed `W/n` scope,
output poisoning and recovery.

Formal licensed external original PDFs remain **7 admitted originals
/ 2 independent original-source W* pixel positives**. All PDFs
generated in this test are test fixtures, not added external
licensed originals. The public `quantapdf_render_page()` and
thumbnail APIs still call PDFium; production SDK linking, install
and package removal under #208 remains unqualified.

## Next integration work

This profile has only **one active direct Clip leaf and one Image Do**.
It does not implement multiple Image operations with distinct
q/Q-restored Clip roots, Clip DAG intersections, transparent soft
masks, Image orientation with negative/sheared CTMs, Form pages,
Path/Image interleaving under a Clip or source text glyph rendering.
Add these as separately bounded source and pure-C11 raster contracts,
then consider the public backend only after exact native feature
admission on representative original PDFs.
