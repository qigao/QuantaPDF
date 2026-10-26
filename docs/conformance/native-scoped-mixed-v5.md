# Native Scoped Mixed Raster V5 — shared frozen Clip per operation

Tracks [#427](https://github.com/qigao/QuantaPDF/issues/427),
[#154](https://github.com/qigao/QuantaPDF/issues/154),
[#214](https://github.com/qigao/QuantaPDF/issues/214) and the
full PDFium exit gate [#208](https://github.com/qigao/QuantaPDF/issues/208).

## Why this private V5 exists

The qualified Native C11 modes V1–V4 already render very small Path/Image
sequences, including a source Image clipped by PDF W/W* followed by a real Q
and unclipped PATH. Maintaining one specialized whole-image compositor per
operation ordering is not a sustainable route to the public page renderer.

The new `quantapdf_ir_raster_scoped_mixed_v5()` **reuses the existing
provider-free C11 Path and RAW Image painters** in the original operation
order. Each image or path has immutable GS/Clip membership; the same one
frozen PATH Clip can be referenced by multiple operations, while an
operation with graphics_state_id=0 draws after Q **without** that Clip.
It performs one final straight-alpha source-over composition only after
all layers have succeeded. No PDF writer, original Graph, PDFium, qpdf or
renderer/provider callbacks are used inside this C11 layer.

## Strict admitted profile

- One selected page, integral width and height, at most **1,048,576 output
  pixels**, optionally transparent background.
- **2–4 operations**, each an opaque solid-filled PATH or normalized RAW
  Gray/RGB IMAGE with optional separate alpha plane, with **at least one of
  each**. Operations may occur in any order; repeated references to
  uniquely-owned RAW image resources are admitted and billed per draw.
- At most **one** frozen Clip PATH and **one** Normal/full-alpha GS, both
  referenced consistently by operation and options. No unreferenced GS/Clip,
  Text, Form, Shading, SoftMask, custom paint, Stroke, non-RAW resource or
  embedded image decoder is admitted. A PATH with an assigned Clip uses
  the same V1 4x4 winding sampler; an IMAGE assigned that Clip uses the
  already-qualified pure-C11 Image mask. Q-restored operations have GS=0.
- Full bounded resource preflight: no more than four layers, four RAW images,
  4,194,304 unique source-image pixels and 4,194,304 cumulative image draw
  pixels; borrowed source buffers must have exact dimensions/components/
  alpha lengths and all image placements must have integer page-contained
  STRETCH bounds. Caller limits can be stricter.
- Global Path and image-mask edge/sample budgets are **conservatively split
  among every layer that requires geometry**, with the same existing V1
  raster checker authoritative for edges, transforms and winding.

The output stays **zeroed on any error**, including late unsupported Path
or Image, missing resource, illegal Clip association, exhausted budget
or failed intermediate layer. All intermediate rasters are disposed and
the original IR is immutable and borrowed synchronously.

## Concrete qualification

- A standalone provider-free C11 test constructs an owned-IR four-layer
  sequence: clipped translucent red IMAGE, clipped blue PATH, Q-restored
  green IMAGE, Q-restored yellow PATH. It tests both nonzero and even-odd
  W/W*, opaque and transparent background, exact interior color goldens,
  repeat/immutable-input behavior and explicit atomic invalid/late budgets.
- The existing original-source `quantapdf.native_source_image_clip` test
  runs **both** the qualified specialized V4 composer and this reusable
  V5 compositor against exactly the same original PDF's detached owned IR.
  Output must be byte-identical before the original-PDFium RGB/oracle and
  fixed colored probes are checked. PDFium remains test-only here.
- The formal external corpus remains **7 byte-admitted originals / 2 W*
  original-source native pixel positives**. Generated PDFs do not become
  new externally licensed originals.

This does **not** admit arbitrary PDF page operators or guarantee a full
mixed Path/Image PDF source lowering. The source parser's bounded mode
admissions remain intact. This V5 is **private**, not a public native
render-page selection, and the installed SDK still includes the production
PDFium renderer. Text, font outlines, Form, general transparency/group,
full device options and unsupported image formats remain #145/#154/#208
work. Do not close those issues on the strength of this subset.
