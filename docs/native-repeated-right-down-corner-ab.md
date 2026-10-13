# #269 — Right-down Stroke corner coverage A/B (private, isolated)

Status: explicitly-invoked **third** private C11 Stroke preview only.
This work is stacked on #286, whose exact HEAD `5156d237` has full
Linux Release+ASan/UBSan, macOS and Windows CI SUCCESS, but whose
corner-endpoint result is NOT original-PDFium pixel parity.

## Grounded immutable-original defect

Original external jsPDF raw bytes (3344, SHA-256
`179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e`)
contain a repeated horizontal 1pt black `S` Stroke turning **downward**
at the upper-right corner. The subsequent `W` Clip is applied only
to the later red `f` operation, never to that earlier stroke.

The #286 endpoint-inclusive candidate improves full-source PDFium-vs-C11
RGB absolute error from #284's 27,735 to **27,141** and leaves only
**one >64-delta pixel**, consistently across Linux, Windows and macOS.
That remaining pixel is exactly original device `(250,19)`:
- C11 endpoint trial: black `(0,0,0)`, Stroke alpha 255.
- Original PDFium: grey `(76,76,76)`, implied partial black coverage.
- It is the outside upper-right device pixel of the source's horizontal
  black Stroke turning down into the next vertical segment.
- At original `(250,20)` PDFium is black, so applying the same partial
  coverage to the inside/lower pixel would regress.

**Thus repeated-line full-row coverage is not the whole rule**.
No evidence authorizes shifting the page, moving the Path or changing
global Path/Clip antialiasing. PDFium AA sample centers are not inferred
or pretended known.

## New independent candidate

A *third*, explicitly-invoked private symbol
`quantapdf_ir_raster_device_polyline_stroke_right_down_corner_preview_v0`
is implemented within the same bounded C11 single-owner module.
It shares all existing NativeDeviceGrid/IR admission, zero-output refusal,
opaque Butt/Miter source-width restrictions and one uniquely owned RGBA
result. Neither the original V0 nor #284's repeated-interior and
#286's endpoint-inclusive preview behavior changes.

The narrow trial requires **all** of:

- horizontal source segment moving right, followed *immediately* by a
  connected strictly vertical source segment moving down (actual
  right-angle path topology, not a device pixel-coordinate special case);
- same-Y repeated source horizontal line segments with geometric
  overlap, at least twice;
- source width exactly 1pt and projected stroke thickness below two
  device rows;
- device pixel interval contains the turn vertex in X; the pixel is on
  the *upper/exterior* side of the vertex and intersects the horizontal
  source stroke strip;
- initial C11 4x4 sampled alpha is nonzero and not already full.

Every other pixel uses the unchanged #286 endpoint candidate, including
the interior/lower right-angle corner row, left repeated endpoint,
single-pass bottom Stroke, vertical Stroke and later red W Clip Fill.

At only this outside-corner pixel, the A/B trial replaces an
unconditionally opaque repeated-row result with a **deterministic
integer straight-alpha repeated-overdraw estimate** derived from
the baseline 4x4 sampled coverage and the number of source segments:
for alpha A in [0,255] and N repeats, independently compute
`255 - 255 * (1 - A/255)^N` by bounded integer steps. This is a
**falsifiable analytical trial**, not an assertion about the PDF
specification, PDFium's raster API or the correct treatment of source
overlap inside a single Stroke path.

The third A/B performs extra geometric scans; it independently
admits `count*3` source-edge sampled-work slots per pixel, rather than
silently reusing the prior `count*2` bound.

## Reproducible tests

A standalone strict C11 test constructs an actual repeated horizontal
source line connected to a downward vertical source stroke:
- #286 endpoint mode predicts an opaque upper-right corner;
- new corner A/B must emit a strictly partial alpha there;
- inside/lower corner and left endpoint remain byte-identical to #286;
- repeated preview is byte-identical on repeat;
- no right-angle corner, source 144dpi width or inadequate sampled-edge
  work must leave unchanged/transactional fail-closed behavior.

The immutable original PDF is separately lowered to owned Detached IR
after Source/Graph release, with no persistence of raw state.
All three independent candidate stroke layers are composed with the
**same original later W-clipped Fill**, in the original paint order,
then read-only compared against every **501,832 original PDFium pixels**
and all **1,505,496 RGB channels**.

The new original A/B JSON contains total and mean RGB error, changed and
gross >64 pixels, max channel delta, deterministic repeated RGBA and
white-RGB FNV64. An independent bounded spatial attribution reports
worst coordinates, real PDFium/C11 RGB and per-source alpha, with the
four partial-coverage error totals reconciled in CMake. **No
inferred passing tolerance or mandatory 'improvement' gate** is
introduced for this experimental corner. All original whole-page
Legacy and Fill-only native paths continue UNSUPPORTED with zero pixels.
No original/W* positive, formal corpus increase or public SDK cutover.

Do not merge this PR or alter #208 on A/B evidence. A better metric on
one 1pt rectangular source still does not prove full PDF compliance.
