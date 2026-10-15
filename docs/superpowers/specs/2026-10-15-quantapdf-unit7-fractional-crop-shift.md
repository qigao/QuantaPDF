# #269 — bounded real-source fractional CropBox perturbation matrix

**Draft / DO NOT MERGE, test-only.** This child follows [Draft #353](https://github.com/qigao/QuantaPDF/pull/353), which measured original-PDFium red bounding boxes at 96DPI for four different intrinsic rotations of an offset-CropBox, leaf-/UserUnit7 page. Those binary edge coordinates varied by rotation. They were observed from **test-generated original PDF bytes**, not approved external publisher originals.

## Discriminating test input

To distinguish rotation-dependent PDFium rasterization from accidental phase alignment in the existing four pages, keep the **same actual W Clip + opaque red Fill PDF content stream**, the same leaf UserUnit7, and the same visible CropBox width=10/height=8 PDF source units. Only translate the source CropBox via three exact decimal variants:

| Variant | Effective CropBox | Change |
|---|---|---|
| 1 | [2.125, 3, 12.125, 11] | X + 1/8 |
| 2 | [2, 3.125, 12, 11.125] | Y + 1/8 |
| 3 | [2.375, 3.375, 12.375, 11.375] | X/Y + 3/8 |

Four /Rotate=0/90/180/270 cases per variant give **12 separate genuine test-generated original source PDF byte streams**, each independently opened through the regular Source→bounded Graph→owned Detached Composer IR pipeline. Source/Graph owners drop before private native Raster and independently reopened source-PDFium rendering. The existing 96DPI grid remains 94×75 or 75×94, **7,050 pixels per case / 84,600 total**, with strict normal capacities and no additional canvas allocation.

On each case observe the original PDFium red-on-white pixel bounds, partial green count, unexpected chroma, contiguous binary rectangle predicate, and per-edge steps relative to **the independently computed Source→Device** fitted grid. Separately record the same-source C11-vs-PDFium full RGB error, pixels different, original-PDFium byte-repeat and unmodified QPDF source input. **Do not assert the shifted PDFium bounds equal a rule inferred from #353**: changes are the evidence this test is meant to uncover.

A separate CMake acceptance test checks every mode, source variant/rotation ordering, bounded bitmap size, step count/range, binary rectangle count integrity and aggregate error consistency. CI uploads Linux Release, Linux ASan/UBSan, Windows, and macOS JSONs. No new C11 Raster algorithm, Graph→Composer matrix, SDK/ABI, fallback, external corpus or release is modified.

Even identical results for these 12 QPDF-generated input PDFs would **not** establish third-party independent-source compatibility. #343's actual reporter PDF remains unverified for original bytes and rights, and no separately released/licensed small painted nonunit UserUnit source is admitted. Maintain #269/#343 OPEN, all stacked PRs Draft / DO NOT MERGE, external-native-pixel positive counts unchanged and public/installed SDK PDFium (#208 backend cutover blocked).
