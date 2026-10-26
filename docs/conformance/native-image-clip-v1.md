# Native RAW Image Clip V1 — private C11 prototype

Tracks [#214](https://github.com/qigao/QuantaPDF/issues/214),
[#154](https://github.com/qigao/QuantaPDF/issues/154) and the
complete PDFium removal exit gate
[#208](https://github.com/qigao/QuantaPDF/issues/208).

## Capability under qualification

`quantapdf_ir_raster_clipped_raw_image_v1()` consumes one
**already-owned, normalized RAW Gray/RGB IMAGE operation** with exactly
one immutable Graphics State and one Clip PATH resource, supporting
nonzero/even-odd winding, affine Clip-definition transform,
page-contained integer STRETCH bounds and optional per-pixel alpha.
It returns independently malloc-owned straight RGBA8 without using PDFium,
qpdf, a provider, Source/Graph or a PDF writer.

The implementation composes two existing independently qualified C11
primitives: a transparent-background RAW Image render and a transparent
white rectangle painted through the existing Clip PATH 4×4 per-subpixel
winding raster. The rectangle matches the selected Image operation
bounds. The mask's quantized alpha is multiplied by the Image alpha,
then composited over the original opaque or transparent page
background using 64-bit integer straight-alpha source-over. There is
**no binary whole-pixel Clip shortcut**, silently skipped resource, or
PDFium fallback.

The admitted IR profile is deliberately exact: one page, one RAW
image resource, one IMAGE operation referencing Graphics State ID 1,
and one direct Clip PATH resource with Clip ID 1. Both state and image
operation own the same immutable resource associations. The profile
rejects Clip intersections/multiple paths, soft masks, non-Normal
blend, partial state opacity, Form/font/text/paint resources, links,
outlines, extra/unused Image operations or resources and unsupported
source Image encoding. Any missing, malformed, invalid reference,
nonfinite geometry, insufficient mask edge/sample budget or RAW Image
pixel/work budget fails before publishing a result.

## Evidence

The standalone `quantapdf.native_ir_raw_image_clip_v1` C11 target
links no PDFium or qpdf, and exercises:

- Triangle Clip coverage at 4×4 subpixels, including fractional edge
  pixels without dropping their partial alpha.
- Even-odd nested rectangular hole, Clip-definition CTM translation
  independent of Image bounds and immutable source geometry.
- Transparent page plus 50% source Image alpha, off-Clip empty pixels.
- Two independent identical renders; the returned pixels outlive
  mutation of borrowed Source/IR payloads.
- Poisoned output slots, invalid operation/graphics-state/Clip IDs,
  unsupported source resource data, late malformed Clip geometry,
  unused resources and explicit budget-exhaustion rejection, followed
  by successful rendering after all rejected attempts.

This **does not yet admit an original PDF** containing a `W/W*`
Clip and a named Image `Do` as a complete mixed source page. The
current strict original Image Do bridge and mixed Path/Image bridge
correctly reject unqualified `W` and `W*` operators. The next
qualification must extend the original-source interpreter with a
bounded, correctly scoped `q/Q` Clip membership and separately
compare unchanged-original PDFium pixels with detached Native output,
including positive and negative real source cases.

This test-only private primitive does not switch
`quantapdf_render_page()`, enlarge the formal externally licensed
original corpus (still **7/2**) or remove SDK PDFium import, links,
runtime copies or license artifacts. #208 remains open.
