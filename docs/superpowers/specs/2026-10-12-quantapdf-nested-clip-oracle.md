# Nested Clip DAG — Composer Writer and PDFium conformance

Tracks [#225](https://github.com/qigao/QuantaPDF/issues/225), supporting
[#214](https://github.com/qigao/QuantaPDF/issues/214) and the PDFium-free
runtime roadmap #208.

## Provider boundary

Private native C11 Clip DAG rendering (PR #224) uses no QPDF/PDFium.
The QPDF Composer **export** route now accepts a bounded acyclic Clip
intersection DAG: validator admits nested intersection members, validates
their references and detects cycles/overdeep graphs without heap allocation,
with a maximum path length of eight nodes and a maximum of 1024 visited nodes
per root. The Writer traverses the graph in deterministic DFS order with
the same depth/expansion caps and emits sequential PDF `W/W* n` operators.
This is equivalent to the intersection of the Clip Path leaves, including
shared leaves (repeated narrowing is idempotent).

The Writer feature is separate from native Raster. It does not make PDFium
a runtime dependency of Native Raster or introduce an automatic fallback.

## Test-only differential oracle

`quantapdf.native_nested_clip_oracle` compares the same canonical owned
IR through:

- Native C11 `quantapdf_ir_raster_clip_dag_v3` directly to RGBA8.
- `quantapdf_qpdf_compose` to PDF bytes, then public PDFium page rendering.

Cases include a 3-leaf nested intersection, a shared leaf DAG, fractional
affine Clip boundary, Even-Odd hole, and cubic Bézier Clip. It verifies
deterministic repeated native bytes, explicit red/white samples, mean
per-channel RGB difference <=10, gross-error (>64) fraction <=1/12, and
cycle rejection with empty PDF outputs. No tolerances may be silently raised
to bypass semantic mismatches.

The test-only executable is the only added target linking QPDF/PDFium;
the separate pure C11 `quantapdf.native_clip_dag_v3` remains independent.

## Not yet implemented

Multi-operation nested Clip state, arbitrary q/Q stream interpretation,
stroke geometry mixed with Clip, color/transparency groups, soft masks, text,
images, forms and PDFium-free public SDK are out of scope. Keep #214/#208
open, and require Linux Release/ASan+UBSan, macOS and Windows product
conformance before merging.
