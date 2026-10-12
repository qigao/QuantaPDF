# Native q/Q source-PDF conformance — original PDF vs native C11 pixels

Tracks [#229](https://github.com/qigao/QuantaPDF/issues/229), under
[#214](https://github.com/qigao/QuantaPDF/issues/214) and the
PDFium-free roadmap [#208](https://github.com/qigao/QuantaPDF/issues/208).

This is the first test that **does not construct the Composer IR by hand** and
does **not** re-export it as a PDF before measuring native rendering.

## End-to-end qualification chain

A test-only QPDF API constructs an actual single-page source PDF with the
ordinary PDF graphics commands `q`, `Q`, `W`, `W*`, `n`, `re`,
`cm`, `rg` and `f`. QuantaPDF opens the source and captures its
owned versioned Graph Snapshot, then passes the selected page through
`quantapdf_graph_snapshot_page_composer_ir`. It closes the document
**and releases the snapshot** before rendering the resulting detached IR
directly using `quantapdf_ir_raster_scoped_operations_v1`.

This is provider-free C11 path/clip rasterization of a **real PDF's
content operators**. No QPDF Composer Writer or PDFium calls execute
between the detached IR and the native pixel buffer.

Independently, the test renders the **original source PDF** through the
existing PDFium-backed public API. Its RGB pixels are compared against the
native straight-alpha RGBA8 result after explicit source-over-white
compositing (since ordinary PDF pages have transparent paint backgrounds but
the PDFium RGB API returns a white composited bitmap).

## Initial deterministic scenarios

- Two nested q/Q scopes and two successive `W n` path clips; red draws
  under their intersection, blue after inner `Q` under only outer Clip,
  green after outer `Q` outside both Clips.
- An inner `W* n` even-odd hole after an outer `W n`; the hole is
  unpainted, blue after restore sees only outer Clip.
- Clip captured with translated definition-time CTM; subsequent `cm`
  changes paint CTM but **must not move the already-defined Clip**.
- Unsupported PDF operator in the original content stream fails strict
  Graph-to-Composer lowering with empty output; no implicit PDFium fallback.

For each accepted source: output owns detached RGBA pixels and reproduces
identical bytes on consecutive independent renders. Fixed interior golden
points must match **both** renderers within 3 RGB values, while full-frame
mean per-channel RGB difference is limited to <=10 and the fraction with
any channel differing by more than 64 is <=1/12. Antialiasing is not
assumed to match bitwise.

## Ownership and exclusions

This test-only C++ fixture links QPDF to create a valid PDF and links
PDFium as a separate reference renderer. The native Raster C11 sources,
standalone native conformance executables, and the public SDK ABI are
unchanged. No new runtime fallback, raw-provider persistence or retry.

The initial source-PDF gate admits only a **single 120×120 unrotated
zero-origin page with default UserUnit**, 2–4 opaque path fills and
normal blend, and a bounded Clip DAG. CropBox, Rotate, UserUnit source
comparisons, larger document corpus, incomplete/malformed PDFs, arbitrary
graphics-state operations, Stroke+Clip, soft masks, text/images/forms and
PDFium-free installed SDK packaging remain #229/#214/#213/#208 work.


## Source multi-stream and sibling-page isolation increment

The same test target now creates a **two-page source PDF**, where page
zero paints the entire canvas blue and page one contains two independently
encoded `/Contents` streams. The first stream leaves an active nested
`q/W` Clip stack and an unpainted path; the second begins by finishing
the `f` paint and later restores `Q` twice before blue and green
painting. This exercises token/graphics state across the stream boundary.

A requested `max_streams = 1` strictly rejects the selected page and
returns an empty IR slot; `max_streams = 2` admits it. Capturing/lowering
page zero produces only its own single paint, with no leaked Clip state.
Capturing/lowering page one yields precisely three path operations and
its owned Clip resources, independent of the other page.

The test drops the document and Graph before repeated direct native V1
scoped DAG rasterization. The only PDFium invocation independently renders
**page index 1 of the original source PDF**. The identical fixed RGB
interior goldens, <=10 mean RGB and <=1/12 gross-error thresholds are
applied across the whole white-composited framebuffer. Page-level
selection and content-array concatenation are thus tested without
PDFium in the native render path, without changing C11 ABI or introducing
an additional test binary/workflow.
