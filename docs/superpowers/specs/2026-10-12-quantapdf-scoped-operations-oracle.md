# Multi-operation native Clip — test-only PDFium differential oracle

Tracks [#214](https://github.com/qigao/QuantaPDF/issues/214)
following merged scoped native C11 renderer PR #222.

Both consumers receive the **same canonical owned IR** with three ordered
opaque solid PATH operations and two Normal/full-alpha graphics states
referencing independent Clip Path resources:

1. Native `quantapdf_ir_raster_scoped_operations_v0`, pure C11: each
   operation renders as a separately clipped transparent layer, then
   source-over in IR order with no cross-operation Clip leakage.
2. Test-only `quantapdf_qpdf_compose` followed by the public PDFium-backed
   page raster API. No PDFium/qpdf code is called from the native renderer.

Four deterministic corpus scenes prove: differently clipped red/blue
operations and subsequent un-clipped green, independently un-clipped paint
**outside both prior clipping regions**, reversed red/blue operation order,
and fractional Clip transform. Every scene must have byte-identical repeated
native pixels; selected interior golden RGB values must match both
renderers; full-page per-channel mean RGB difference must be <=10 and gross
>64-channel-difference pixels must be <=1/12 of the page.

Only this **conformance-only** target links PDFium and qpdf. The existing
`quantapdf.native_scoped_operations_v0` C11 test remains entirely
provider-free. No public C11 ABI or installed SDK changes; no runtime
fallback and no automatic retry. The native implementation is intentionally
bounded to 2–4 opaque solid PATH operations, optional direct Clip Paths,
Normal blend and full alpha. It is **not** an interpreter for arbitrary
q/Q streams, nested Clip DAG, transparency/soft masks, font, images, forms,
or Stroke. Remaining coverage stays tracked by #214/#213/#208.
