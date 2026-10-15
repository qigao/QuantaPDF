# #269 — Small real test-generated original PDF with `/UserUnit 7`

Test-only source-PDF backed exact-RGB conformance, distinct from:

1. [#335](https://github.com/qigao/QuantaPDF/pull/335) eight
   generated-original W Clip+Fill tests with Unit 1/2 (strict zero-RGB).
2. [#346](https://github.com/qigao/QuantaPDF/pull/346) large
   **synthetic geometry** from a public /UserUnit 7 reporter description,
   checking preallocation refusal under 1M-pixel/3MB-RGB caps.
3. [#343](https://github.com/qigao/QuantaPDF/issues/343)
   a distinct independently uploaded /UserUnit 7 PDF whose **original
   bytes and redistribution rights remain unverified**.

## Positive, explicitly bounded original-source case

The existing QPDF **test writer** creates a genuine one-page PDF document
with the inherited MediaBox [0 0 20 30], offset CropBox [2 3 12 11],
intrinsic Rotate 0/90/180/270 (alternately inherited/leaf), a real leaf
`/UserUnit 7`, one nonzero W Clip and opaque red Fill, with identity PDF
content CTM. Each page is a **separate test-generated original source
PDF**, not a re-export of the Composer IR. It is sufficiently small to
satisfy independent explicit C11 raster budgets, unlike #346's long page.

The original bytes flow Source→Graph Snapshot→independently OWNED
Composer IR; Source and Graph owners are dropped before private C11
device rendering. The **test-only** PDFium backend renders the
independent original file, never a native IR re-export. Only the final
visible-IR→device scale is applied by the C11 raster: no second
Source CropBox/Rotate/UserUnit affine application.

Four cases:

| Original intrinsic Rotate | /UserUnit | Original visible page at 72 DPI | Original RGB pixel count |
|---|---:|---:|---:|
| 0 | 7 | 70×56 | 3,920 |
| 90 | 7 | 56×70 | 3,920 |
| 180 | 7 | 70×56 | 3,920 |
| 270 | 7 | 56×70 | 3,920 |

Each case asserts:
- Detached IR has exactly one Fill operation, Clip and GraphicsState.
- Device Grid has independently bounded 3-byte RGB extent and Q4
  `pixel_count × 16 × 8` work capacity; RGBA8 private renderer has a
  separate 80k output cap.
- C11's repeated original-source render is **byte-identical**.
- A deliberate mismatch between source IR's /UserUnit=7 and supplied
  metadata `UserUnit=1` is strictly `UNSUPPORTED` with **zero** output.
- PDFium original interior red sample and exterior white sample agree
  with explicit source-space goldens.
- Across every RGB channel of every page pixel, C11 straight RGBA8
  composited over white must match **original PDFium RGB with zero
  absolute difference**. A *single* different channel must fail CTest.
- All file outputs use a **different temporary PDF filename** from the
  existing Unit1/2 CTest, preventing parallel CTest source races.

Strict, independently captured JSON reports are uploaded on Linux Release,
Linux ASan/UBSan, macOS and Windows, including each original rotation,
RGB error/different/gross/max status and totals. The new schema flags
`source_kind=test_generated_original_pdf`,
`generated_source_exact_rgb_parity=true`, and
`external_native_positive_increment=0`.

## Not qualified

This test **does not** establish an independently published,
license-reviewed, hash-pinned original `/UserUnit 7` pixel positive.
It does not introduce a production renderer, SDK/ABI switch, fallback,
formal external corpus change, or general Stroke/W* support. The
uploaded issue `Userunit.pdf` remains distinct, and the separate
py-pdf four-page crop-rotation original contains **no /UserUnit key**.
Requested rotation and arbitrary additional content CTM remain blocked.

Keep QuantaPDF #269 and #343 OPEN, all stacked PRs Draft / DO NOT MERGE,
installed/public SDK PDFium, #208 backend switch blocked.
