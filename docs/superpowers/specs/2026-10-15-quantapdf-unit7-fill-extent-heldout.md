# #269 — Held-out original PDFium Fill-only geometry extent and origin matrix

**Test-only, private, Draft / DO NOT MERGE.** Parent [Draft #356](https://github.com/qigao/QuantaPDF/pull/356) was qualified by [CI #38745674228](https://github.com/qigao/QuantaPDF/actions/runs/38745674228) across Linux Release, Linux ASan/UBSan, macOS and Windows. Four original source-backed reports agreed:

| Primitive (16 QPDF generated source modes) | Fitted Graph floor-min/ceil-max exact / edge abs | Nominal DPI exact / edge abs |
|---|---:|---:|
| Combined Fill under W Clip | 4/16 / 16 | 7/16 / 10 |
| Fill only | 4/16 / 24 | 9/16 / 14 |
| **W Clip only** | **16/16 / 0** | 8/16 / 12 |

Parent #355 also proved all 16 combined PDFium red masks equal the pixelwise AND of independent Fill-only and Clip-only source PDFium red masks, with no missing or additional pixels. The original 16 Fill-only bboxes happen to maintain **57×38** pixels for unrotated/180° pages and **38×57** for 90°/270° pages, while Clip-only integer dimensions can change with CropBox fractional shifts. These observations do not yet prove how Fill rasterizes an altered shape.

## Held-out source rectangles

The QPDF test-only writer retains its original three production-neutral content modes unchanged, and adds **three independent Fill-only source PDFs** with distinct Path rectangle dimensions/locations:

| Shape ID | New source content command | Purpose |
|---|---|---|
| 0 | `1 0 0 rg 4 5 5 4 re f` | Width changes 6→5, height stays 4 |
| 1 | `1 0 0 rg 4 5 6 3 re f` | Height changes 4→3, width stays 6 |
| 2 | `1 0 0 rg 4.125 5.125 5.625 3.375 re f` | Both translation and dimensions fractional |

Each source PDF retains original MediaBox [0,0,20,30], the known four CropBox placements (baseline, X+0.125, Y+0.125, XY+0.375), leaf `/UserUnit 7`, and intrinsic rotation 0/90/180/270. The content stream uses an identity content CTM and exactly one opaque red Fill, without Clip or Stroke. Three shapes × four CropBox positions × four rotations create **48 distinct test-generated ORIGINAL PDF byte streams** with **338,400 measured original PDFium bitmap pixels** at 96 DPI. The separate source filename prevents races with all previous CTest targets.

Every source enters the normal capped Source→Graph page metadata path, with the source document dropped before geometry queries and the Graph snapshot dropped before PDFium rendering. The source PDF is rendered **twice** by the installed PDFium-backed test API; its raster bytes must repeat exactly, and the source file bytes must be unchanged after both renders. RGB8 dimensions are explicitly checked against the already qualified integer Graph device grid (94×75 or 75×94) before bitmap access.

## Five explicitly bounded, predeclared numerical hypotheses

For each source rectangle, project all four source-space corners into:

- **Fitted grid:** the exact existing `quantapdf_graph_page_device_grid` source→pixel Y-down affine;
- **Nominal grid:** the existing Graph source→visible-page affine multiplied by DPI/72 with independent Y-down inversion.

Only values already within 1e-9 of an integer are stabilized against one-ULP decimal parsing. The five pure numeric bounding-rectangle models compared against the independent *original-PDFium observed* red bbox are:

1. Fitted `floor(min)`, `ceil(max)` (Clip-like outward bounds).
2. Fitted `floor(min)`, `floor(max)`.
3. Fitted `floor(origin)` plus `ceil(unrounded shape span)`.
4. Fitted `round-nearest(origin)` plus `ceil(unrounded shape span)`.
5. Nominal-DPI `floor(min)`, `ceil(max)`.

All five models produce **four integer edges only**; no alternate bitmap or competing raster, fallback, or raw source state is allocated/persisted. The observed PDFium RGB is independently classified as red/white/other, with explicit red counts, bounding rectangles, and whether each bitmap is a complete binary filled rectangle. The strict CMake checker validates the 48 source rows, 5 model bboxes/errors per row, per-shape and per-rotation ordering, actual PDFium grid and source-repeat counts, model totals and all no-promotion fields.

Do **not** preselect a model or claim exact pixel conformance merely because a candidate reproduces some rectangles. This is a deliberately held-out **test-generated** rectangle-size variation, not an independently published/licensed PDF source. All existing Stack PRs remain Draft / DO NOT MERGE. #269/#343 stay OPEN, third-party Unit7 reporter original raw bytes/SHA-256 and redistribution rights remain unverified, formal independent original positive count unchanged, and the public/installed SDK remains PDFium with #208 backend switch blocked.
