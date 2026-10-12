# Native Polygon Clip — test-only PDFium differential conformance

Tracks #214, following pure C11 native Polygon Clip V2 in PR #220.

The CI-only executable `quantapdf.native_polygon_clip_oracle` creates
canonical privately owned IR containing one opaque paint operation and
one direct Clip Path resource (plus its normal full-alpha Graphics State).

The exact same IR is consumed in two independent ways:

1. Native C11 Raster V2: sample-wise intersection of paint and clip winding
   at 4×4 subpixel locations, with bounded geometry budgets.
2. Test-only QPDF Composer export followed by the existing PDFium-backed
   public rendering API.

The corpora contain: oblique triangular Clip, an even-odd hole, a fractional
affine-translated rectangle, and a cubic Bézier arch. For every case, the
native output must be repeated-byte-deterministic, interior samples must
match explicit red/white expectations, mean per-channel RGB error versus
PDFium must be <=10 and the number of gross-error (>64) pixels must be
<=1/12 of the page area. Antialiasing identity is not assumed; tolerances
cannot be silently widened to make a semantic mismatch pass.

Only the differential oracle target links PDFium/qpdf. Production raster
and the pure C11 `quantapdf.native_clip_path_v2` test have no provider
linkage, no fallback and no public ABI changes.

This is still a limited one-Clip, one-operation subset. Nested Clip DAG,
changing q/Q graphics state, composed multiple paint operations, clipping
strokes, color groups and soft masks remain separate #214/#208 work.
