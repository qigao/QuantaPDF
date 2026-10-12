# Native Clip Intersection: test-only PDFium differential oracle

The producer/runtime for Native Clip intersection remains pure C11 and does not
call PDFium or qpdf. This conformance-only test deliberately links both
providers to compare **the same canonical immutable IR** via separate paths:

1. Direct `quantapdf_ir_raster_clip_intersection_v1` -> RGBA8 pixels.
2. `quantapdf_qpdf_compose` -> PDF bytes -> public PDFium-backed
   `quantapdf_render_page` -> RGB pixels.

Fixtures cover 2-leaf and 3-leaf flat rectangular intersections, exact
interior red/white samples, repeat byte stability, full-page per-channel mean
difference <=10, and <=1/12 gross-error (>64) pixels to accommodate
differences in antialiasing near clip edges.

Only the registered `quantapdf.native_clip_intersection_oracle` CTest
depends on PDFium/qpdf; `quantapdf.native_clip_intersection_v1`
remains independently compiled pure C11, with no QuantaPDF/PDFium/qpdf links.
The public C11 ABI, installed SDK and production renderer are unchanged.

This does **not** certify arbitrary Clip shapes, fractional coverage,
transformed clips, nested Clip DAGs, q/Q restoration or multiple painted
operations. These remain #214/#208 requirements.
