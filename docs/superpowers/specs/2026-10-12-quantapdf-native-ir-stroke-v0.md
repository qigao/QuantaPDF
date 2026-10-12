# Native IR Stroke V0: strict single-line butt cap

Tracks [#213](https://github.com/qigao/QuantaPDF/issues/213) beneath
[#208](https://github.com/qigao/QuantaPDF/issues/208).

This is the **first** native Stroke increment. It is intentionally far
smaller than the full #213 requirement.

## Algorithm

`quantapdf_ir_raster_stroke_butt_v0()` consumes a selected immutable
`quantapdf_ir`, accepting only one open, opaque `MOVE -> LINE` path with
positive nonzero width, butt cap, identity transform, no dash, no fill and no
resources. It computes a perpendicular half-width normal, creates four
corners, and renders the closed quad using the existing independently tested
4×4 antialiased native V1 fill consumer. The input IR is never modified or
persisted, and all pixel/edge/operation/sample work limits remain enforced
by Raster V1 before output publication.

This conversion preserves straight single-segment butt-cap geometry, but
**cannot** safely approximate joins between multiple segments, miter clipping,
round/square caps, variable dashes, hairlines or transformed stroke widths.
Those requests return `UNSUPPORTED` and leave output empty. Text, image,
form, graphics state, clip resources, alpha/blend and fills also remain
unsupported for this entry point. No PDFium fallback.

## Regression gate

`quantapdf.native_ir_stroke_butt_v0` is a strict standalone C11 test
that links no QuantaPDF runtime, PDFium, qpdf or Composer. It covers horizontal
and diagonal geometry, deterministic repeated bytes, transparent and opaque
background, out-of-bounds work budgets, degenerate length, disallowed cap,
combined fill/stroke, transform, dash, clip resources and error-reset behavior.

The existing `quantapdf.native_ir_raster_oracle` applies to opaque *fills*.
Dedicated test-only PDFium stroke comparisons for #213 (cap/join/miter/dash)
are still required before #213 can close. The public C11 ABI and default
PDFium-backed `quantapdf_render_page` are unchanged.
