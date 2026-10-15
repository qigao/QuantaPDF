# #269 / #343 — Oversized /UserUnit 7 reporter scenario: preallocation refusal

This test-only gate is **not an external PDF original**, not a PDF
producer-compatibility or pixel-positive result. It uses a *synthetic
geometry constructed from public issue text* and does not download,
modify, vendor or claim ownership/licensing for the attached original
`Userunit.pdf` in [dmester/pdftosvg.net Issue #16](https://github.com/dmester/pdftosvg.net/issues/16).

The reporter described an approximately 35-meter-long drawing whose
effective page scale uses `/UserUnit 7`. The exact original PDF bytes,
metadata and attachment redistribution rights remain unverified and
tracked separately by [#343](https://github.com/qigao/QuantaPDF/issues/343).
The test deliberately **does not assert the reporter's textual SVG
dimensions as verified PDF page dimensions**.

A selected hypothetical PDF user-space page of
`[10,20,14183.2,360.562]` and `/UserUnit 7` generates over
200 million device pixels at 72 DPI. This is much larger than the
current private geometry budget of **1,000,000 pixels / 3,000,000 RGB8
output bytes**. The public/installed SDK is unchanged.

The isolated strict C11 test invokes existing
`quantapdf_graph_page_display_map`,
`quantapdf_graph_page_device_grid` and
`quantapdf_graph_device_grid_admit_q4_work`, under all four intrinsic
rotations and DPI 72/96/144:

- Explicit structural geometry-only positive with large bounds at 72 DPI
  (metadata only, no RGB/RGBA buffer created or heap allocation).
- All 12 normal-budget page/DPI combinations **UNSUPPORTED before
  output**, with every grid output byte zeroed on refusal.
- Four further 72-DPI refusals where the geometry pixel count is
  structurally representable but the *output byte* cap is inadequate.
- Four 72-DPI 4x4 work-budget refusal checks, before any sample iteration
  or painting.
- One exact subpixel roundtrip at one device pixel per right-angle page
  rotation to verify the huge source/device scale without allocating a
  200-million-pixel image.

The long-page scenario verifies **capacity and ownership only**, not a
licensed external original, nonzero UserUnit pixel parity, Stroke,
W/W* or arbitrary page rendering. Report no external positive count and
do not promote into `tests/external_source_corpus/manifest.json`.

This gate is an additive **C11 test-only** change. The actual C11 page
device implementation, installed PDFium SDK, ABI, public renderer and
#335 original jsPDF/exact generated-source pixel baseline stay unchanged.
Keep QuantaPDF #269 / #343 OPEN and this/ancestors Draft / DO NOT MERGE.
