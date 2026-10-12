# Original-source PDF page geometry → Native Raster conformance

Tracks [#229](https://github.com/qigao/QuantaPDF/issues/229) and
[#214](https://github.com/qigao/QuantaPDF/issues/214). Extends the existing
`quantapdf.native_source_qq_oracle` rather than creating a redundant test
binary or another CI workflow.

## Evidence chain

The test-only QPDF writer creates a valid one-page PDF with inherited
`/MediaBox [20 30 220 150]`, `/CropBox [40 40 140 100]`, /Rotate and
a leaf `/UserUnit`. CropBox and Rotate are placed alternately on
the page-tree parent or the selected page. The source PDF has nested
`q/Q`, two path clips (`W n`), three independent opaque paint
operations, and a final unclipped green paint.

The document is opened and captured into a Graph Snapshot; the selected
page is lowered to detached Composer IR via
`quantapdf_graph_snapshot_page_composer_ir`. The document and Graph
Snapshot are released **before** direct
`quantapdf_ir_raster_scoped_operations_v1` calls. Native C11 raster
never invokes the Composer PDF writer or PDFium. The original source
file is independently rendered via PDFium for comparison.

## Matrix and measurements

Cases: rotation 0/90/180/270 crossed with UserUnit 1/2, using different
inherited/leaf placements of CropBox and Rotate. Expected page
dimensions come from independently specified crop geometry (100x60)
and rotation, scaled by UserUnit; tests do not use the Graph's map
to derive expected golden pixel coordinates.

For each case, compare full RGB images from native straight-alpha
over-white output and original PDFium RGB pixels. Keep mean channel
difference <=10 and gross-error (>64 per channel) pixels <=1/12.
Check independent interior red, blue, green and white locations, covering
a nested Clip intersection, restored outer Clip, and an operation drawn
after restoring all Clip state. Byte-identical native repeat renders
prove determinism after source ownership is released.

## Boundaries

This qualifies specific positive page-box and transform fixtures for the
bounded 2–4 operation opaque Path subset. It does not imply full CropBox
error recovery, complex content operators, page rotation with text/image,
soft masks, color management, font or annotation parity. PDFium remains
the public SDK raster backend, but is **test-only** for this native
path/Clip execution. The prior standalone pure C11 Native Raster tests
remain PDFium/qpdf free. No fallback, installed ABI change or retry.
