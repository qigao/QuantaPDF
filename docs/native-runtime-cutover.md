# Native QuantaPDF runtime — PDFium removed

This repository's production runtime is **PDFium-free**. The public C11
`quantapdf_render_page` and `quantapdf_render_page_with_options` use the
existing **one** Source V10 (QPDF structure/Flate decoding) → detached IR →
pure C11 native raster → owned RGB bitmap pipeline. No provider retry,
reference renderer, PDFium runtime import/library, PDFium package or licensing
install is permitted. All unsupported features return
`QUANTAPDF_ERROR_UNSUPPORTED`, with an empty output handle; they never
partially paint or select a fallback.

## Currently implemented native rendering subset

- One caller-selected, original, unencrypted page with strict bounded
  PageTree index/ancestor validation, exact 72/96/144 DPI and
  Source V10 original /UserUnit factor on a strictly validated
  1/4-factor lattice from 0.25 to 4. Its leaf numeric token is
  checked independently of QPDF geometry, and after full Content EOF
  admission it scales frozen PATH/W Clip/RAW Image positions (not RAW
  samples) into physical 1/16-point geometry before the unique C11
  native DPI projection. All Media/Crop and full IMAGE work are
  independently charged after physical scaling, with no bitmap
  resampling or alternate provider.
  Nonzero page indices select their true original Page/Contents/Resources,
  never silently substitute the first page. The Source V10 M0 subset
  also accepts bounded integer signed MediaBox lower-left origins, with
  a full-page or a distinct CropBox. Source V10 now also admits
  ORIGINAL decimal Media/Crop endpoints on a strict 1/4-point lattice,
  including fractional Media∩Crop viewport SPANS. The checked original
  quarter-point source intervals feed exact 72/96/144 DPI integer-ceiled
  device dimensions. Original unrounded physical viewport boundaries
  are sampled as a borrowed implicit Clip in the SAME C11 V5 4x4 grid
  as PATH/W Clip/IMAGE. No fit/stretch, post-raster crop or fallback. Inherited Crop and Crop partly outside Media
  remain supported. Original CTM/PATH/W Clip/IMAGE
  coordinates are translated to this visible viewport before IR
  publication; the complete original PATH/Clip geometry and RAW image
  placement (including negative/outer edges) are still preserved.
  The canonical C11 4x4 mask samples true IMAGE rectangle∩W/W* Clip
  at the viewport boundary, retaining the full original XObject
  nearest-neighbor sampling coordinates and original layer order.
  No post-raster bitmap crop or rescales. Fractional original viewport
  spans (not merely fractional origins) and UserUnit≠1 remain unsupported. Only the selected,
  fully qualified source is rebased to the native local one-page IR slot
  zero for the same C11 V5 Raster. Public render-option quarter turns of
  **0/90/180/270 degrees** are admitted at each of these DPIs, after
  full original Source V10/C11 Raster admission on the requested target
  grid. The existing C11 device planner validates rotation dimensions
  and the one-million-pixel quota before an exact row/column RGB
  permutation. Original PDF page-tree /Rotate 0/90/180/270 is also
  admitted independently: Source V10 validates the effective inherited
  quarter turn under a bounded ancestry quota, completely parses the
  unrotated original content, and only then rotates staged PATH/Clip
  points, Image bounds and owned RAW samples before IR publication.
  Render-option display rotation remains a separate operation after
  native C11 Raster. At 96/144 DPI, owned Source V10 IR is projected
  directly into the target-resolution C11 Raster grid before display
  rotation. 144 DPI uses exact twofold geometry;
  96 DPI samples fractional 4/3 PATH/Clip and IMAGE edges in the SAME
  4x4 native device sample grid. The IMAGE footprint
  and W/W* Clip use one sample-intersection mask, rather than multiplying
  separately averaged coverage. RAW image colors use pixel-center
  nearest-neighbor sampling with clamping at fractional edges; optional
  source alpha is multiplied by that one geometric coverage.
  Nonorthogonal angles, region clip and alpha-mode changes remain
  unsupported.
- Public thumbnails now use the SAME fully validated native 72-DPI page
  bitmap and bounded deterministic RGB area downsampling, preserving
  aspect ratio with no upscaling and no second PDF parser/provider. All
  unsupported original content still fails closed before resizing.
- Native blank pages with no painting (missing/empty Contents or
  balanced q/Q and transform-only content) produce owned opaque white
  bitmaps; **unsupported** Text, page annotations and unknown operations
  never become silent blank output. Qualified embedded-TTF Text is a
  separate original Source V10 paint profile with actual glyph contours.
- One to **four unclipped, opaque Path Fill** operations with finite
  integer source geometry and exact normalized RGB/Gray solid colors
  render directly through the SAME Source V10 → uniquely owned IR →
  pure C11 Raster/Context Bitmap path. This uses the pre-existing
  four-layer C11 limit, checks bounded global edge/sample work and
  original source order, and never invents an Image/Clip resource. A
  fifth PATH, late unsupported command or hidden resource refuses
  with an empty public Bitmap and thumbnail.
- One to four **unclipped Image XObject Do** paints (without PATH) are
  admitted through the same V10 reader, bounded RAW/Flate Predictor and
  exact DeviceRGB/Gray Decode, uniquely owned IR, canonical C11 Raster
  and public Bitmap/thumbnail. Source order and every Image/draw budget
  remain enforced; no synthetic PATH/GS/Clip or fallback.
- Exactly one unclipped Image and one opaque solid PATH Fill, in **either
  original source order**, are also admitted through the same V10 Source
  and C11 layered Raster; pixel overlap follows source order.
- **M2a — independently qualified opaque Text/Image:** exactly one
  embedded CIDFontType2 FontFile2 TrueType show with **black, opaque
  glyph outlines** and one bounded opaque DeviceRGB/Gray RAW or
  FlateDecode Image XObject Do, in **either original paint order**.
  Source V10 parses the complete original page once, retains no QPDF/font
  owner in the detached IR, and admits only the sole referenced /Font
  and /XObject resource categories. The one Source-owned compiled C11
  V5 page executes its existing exact-Text contour geometry and image
  layer on the same physical 72/96/144-DPI grid, one original source
  order and one aggregate work/edge/source-pixel/drawn-pixel/byte
  ledger. The black glyph coverage derived **in-place** from the
  pre-existing exact opaque Text-on-white result preserves historical
  edge rounding before V5's existing source-over compositor. No new
  parser, raster, installed public API, interim full-page renderer or
  PDFium fallback is added. After both successful paints, a late
  unknown Do/Fill/W Clip/Tr/CTM, extra /Resources, SMask or unbalanced
  source state refuses the **entire** page with empty output, never a
  partially painted Bitmap. In this **M2a** profile, more than one
  Text/Image paint, colored Text, arbitrary fonts, PATH Fill/Clip,
  general graphics-state scopes and alpha remain unsupported; those
  are separately tracked by M2b [#580](https://github.com/qigao/QuantaPDF/issues/580).
- **M2b.1 — first three-paint original Text/Fill/Image page:** one
  black opaque embedded CID TrueType Text show, one opaque solid
  axis-aligned rectangular PATH Fill, and one RAW/Flate Gray/RGB Image
  Do in the two independently tested source orders **Text→Fill→Image**
  and **Image→Fill→Text**. A single original QPDF Source V10 token pass
  records every completed Text/Fill/Image event; one uniquely-owned
  three-operation IR preserves their exact authorial order/rectangle
  geometry/image samples. The same retained Context and native C11 V5
  page compile the original glyph outlines, Fill V1 geometry and RAW
  Image as three source-over layers under ONE shared physical
  72/96/144-DPI work/edge/source-pixel/drawn-pixel/byte ledger. Every
  output RGB pixel is compared with pre-M2a reviewed Roboto coverage
  plus independently calculated 4×4 Fill/Image geometric coverage
  and image center-nearest samples; original M1/M2a exact pixels
  remain unchanged. Checked N/N−1 and real heap fault/rollback tests
  preserve atomic output and post-Context Bitmap lifetime. Any fourth
  unsupported paint, extra Font/XObject/ExtGState, late unknown Do/W
  Clip/Stroke, nonblack Text or unbalanced q/Q/ET refuses the WHOLE
  page; no partial Bitmap/provider retry is allowed. No second PDF
  interpreter, full-page renderer, public SDK ABI or PDFium import is
  introduced. This does NOT qualify general mixed Text+Clip, wider
  fonts/colors, Forms or alpha: [M2b #580](https://github.com/qigao/QuantaPDF/issues/580)
  and [M2b.1 #582](https://github.com/qigao/QuantaPDF/issues/582)
  track the bounded first increment and remaining work.
- One or **two source-ordered Image-only Do paints** under the SAME
  genuine frozen W/W* Clip leaf, with balanced q/Q and no extra PATH
  paint, use the existing C11 clipped RAW Image raster for each layer.
  Ordered overlapping pixels use the exact alpha source-over painter.
  All drawing outside that Clip stays white; a third Image, a second
  Clip or a post-Q unclipped Image is rejected, not silently painted.
- One opaque solid PATH Fill and one decoded Image Do in EITHER authorial
  order beneath the SAME frozen W/W* Clip also render via the existing
  C11 source-over compositor and Context Bitmap. A third all-clipped
  paint or second Clip refuses atomically; no PDFium-based approximation.
- Four-layer homogeneous PATH-only and IMAGE-only admission remains
  distinct from the **one IMAGE + one PATH** unclipped mixed profile and
  the narrower W/W* clipped IMAGE profiles. These **PATH/Image-only**
  contracts do not independently imply arbitrary source Stroke, Text,
  Clip or extra mixed/clipped operations; the explicit M2a Text/Image
  profile above is separate and narrower.
- One frozen W/W* Clip, correct Q restoring scopes, 2–4 source-ordered
  8-bit Gray/RGB Image and opaque Path Fill operations, strictly bounded
  to at least one operation of each type for that **mixed** subset.
- Direct Image XObject RAW/Flate, explicit Decode identity/inversion,
  Predictor 1/2/10..15 with checked Colors/Columns/BitsPerComponent.
- Finite normalized RGB/Gray solid fill colors and exact owned RGB output.
- Direct 96/144 DPI target painting retains existing source/image/work
  quotas and four-operation limit. Source pixels are never mutated and
  projected PATH/Clip commands are borrowed only for the synchronous
  C11 raster call; no new provider or public ABI is introduced.
- Unqualified text (including arbitrary font formats, multiple fonts
  and unsupported embedded glyph transforms), annotation drawing,
  forms, soft masks, gradients, advanced strokes, transparency,
  alternative Image formats, general page geometry and unqualified
  render options fail closed. Qualified CID TrueType glyph outlines
  and the bounded opaque Text/Image case above are not rejected.

## Native feature development and test gate

The native-only suite checks deterministic fixed RGB goldens, exact repeated
bitmaps, source-document ownership teardown, invalid-device and late-source
error reset, pure C11 Clip/Raw Image/scoped mixed Raster and device planning.
The M2a original embedded-TTF/RAW and embedded-TTF/Flate checks use
**pre-M2a reviewed Roboto RGBA goldens** plus an independently authored
4×4 Image-coverage, center-nearest RGB and source-over oracle for every
72/96/144-DPI pixel in both paint orders. M2b.1 adds an independently
sampled original opaque Fill in both three-paint orders and verifies
the same grid and source-over through the SAME public Bitmap path.
Independent tests cover all actual Source C-allocations, original
C11 exact-Text malloc 1–4, every additional PATH/Image/composite
allocation position, bounded global N/N−1 work/edge/source/drawn/
output-pixel/byte quotas and late unsupported original-resource
refusals. Source/OOM and public negative original-PDF tests remain
separate bounded invocations of existing native executables. No
reference PDF renderer comparison, downloaded PDFium binary, differential
renderer or self-generated mixed-image golden is used in build/CI.

Next [M2b #580](https://github.com/qigao/QuantaPDF/issues/580)
extends ordered Text/Image/Fill and scoped W/W* Clip with true
**correlated 4×4 glyph ∩ Clip sample intersection** and frozen q/Q
graphics-state semantics; premultiplied/double-rounded alpha-mask
multiplication is not an acceptable substitute. Preserve the unchanged
C11 work/resource limits and independent whole-image references. Further
native features must extend the **same** source grammar, owned IR,
raster painter and device pipeline with explicit negative budget,
ownership and original-source tests. Do not restore a PDFium fallback.

The public C11 ABI remains unchanged. The SDK is a C ABI backed by C/C++
implementation; qpdf 12.4, ZLIB and JPEG remain private structure/codec
dependencies. There is no installed PDFium import or runtime.
