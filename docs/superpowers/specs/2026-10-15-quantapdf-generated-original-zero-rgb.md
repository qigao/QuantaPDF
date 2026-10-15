# #269 — Strict zero RGB gate for eight generated original PDFs

Stacked exactly on Draft #334 HEAD `dc5b8dedb0ad09cf5c09696ddd1c7201bbe08fa0`.
The parent [full CI #38703920900](https://github.com/qigao/QuantaPDF/actions/runs/38703920900)
passed Linux Release+ASan/UBSan, Windows and macOS; four
independently uploaded `generated-visible-rgb-*` reports agree on **zero**
absolute RGB error, **zero** differing pixels, **zero** maximum channel
error and **zero** >64-gross pixels across the same eight actual test-owned
original source PDFs (1,600 page pixels total).

## Promotion within a private generated-source gate only

The separate `quantapdf.native_generated_visible_page_rgb_report` CTest
now fails if **any** generated original PDF differs from its independently
rendered original-PDFium RGB by even one channel unit. This is a strict
zero-tolerance **test-owned** source gate, in addition to the existing
semantic interior/exterior goldens and byte-identical repeat. It
does not call PDFium from the C11 Raster and does not change the native
pixel implementation or any source PDF bytes.

The report additionally declares
`generated_source_exact_rgb_parity=true`, while retaining
`observation_only=true`, `pixel_parity_qualified=false` and
`external_source_positive_increment=0`. These fields distinguish a
proven *generated* PDF case from a separately licensed/published source.

## Still blocked

The tested source page has a single opaque red Fill and one ordinary
nonzero W Clip, a bounded rectangular geometry, identity content CTM,
offset CropBox, intrinsic Rotate 0/90/180/270, UserUnit 1/2, and
default original-PDFium device dimensions. General Stroke, multiple
Clips, W*, complex CTM, requested rotation, expanded pixel-DPI
comparisons and independently published producer-diverse PDF samples
are **not** accepted by this zero-error gate.

Keep QuantaPDF #269 OPEN, this PR and all stacked ancestors Draft /
DO NOT MERGE, formal external counts unchanged, public SDK PDFium and
#208 backend cutover blocked. No release, merge, or SDK modification.
