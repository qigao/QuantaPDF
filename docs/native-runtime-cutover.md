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
  PageTree index/ancestor validation, exact 72/96/144 DPI and UserUnit 1.
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
  bitmaps; text, page annotations and unsupported operations never
  become silent blank output.
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
  the narrower W/W* clipped IMAGE profiles. None of these modes imply
  source Stroke, Text, arbitrary Clip or extra mixed/clipped operations.
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
- Unsupported text, annotations drawing, font glyphs, forms, soft masks,
  gradients, advanced strokes, transparency, alternative Image formats,
  general page geometry and unsupported render options fail closed.

## Native feature development and test gate

The native-only suite checks deterministic fixed RGB goldens, exact repeated
bitmaps, source-document ownership teardown, invalid-device and late-source
error reset, pure C11 Clip/Raw Image/scoped mixed Raster and device planning.
There is no reference renderer comparison, downloaded PDFium binary or
differential pixel oracle in the build/CI.

Next native implementation tasks should extend the **source grammar, owned IR,
raster painter and device pipeline** directly, gated by feature-specific
fixed fixtures and negative work/quota/ownership tests. Do not restore a
PDFium fallback to fill any gap.

The public C11 ABI remains unchanged. The SDK is a C ABI backed by C/C++
implementation; qpdf 12.4, ZLIB and JPEG remain private structure/codec
dependencies. There is no installed PDFium import or runtime.
