# #269 — Independent ORIGINAL PDFium bitmap extent vs sealed Graph device grid

Draft test-only child of [#344](https://github.com/qigao/QuantaPDF/pull/344)
at HEAD `c8c79b80bd5164cfac823b387747b6d4a591fa81`, which passed
[full CI #38717560411](https://github.com/qigao/QuantaPDF/actions/runs/38717560411)
on Linux Release+ASan/UBSan, Windows and macOS. That gate qualified
the **unchanged upstream published original** pypdf/PyMuPDF four-page
PDF for /CropBox + intrinsic /Rotate 0/90/180/270 geometry at
72/96/144 DPI, with 12 exact bitmap bounds, 768 Q4 source/device
roundtrip probes and bounded failure-zero tests.

## Independent PDFium oracle — separate from native image pixels

This child adds a test-only mode to the existing original-source oracle:
`--quarantined-pypdf-original-pdfium-dimensions`.

It opens **exact original PDF bytes** through the already installed
test-only PDFium-backed renderer, creates 12 actual RGB8 images
(4 pages × 72/96/144 DPI), and compares **PDFium's real output bitmap
width and height** to the pre-established *independent* sealed-Graph/C11
device-grid predictions from the parent. For every rendered frame it
checks bounded pixel and byte extents, that the actual original PDF
contains nonwhite pixels, and that repeating PDFium rendering yields
byte-identical RGB output on the same platform. Each report records
source page, DPI, actual PDFium dimensions, real nonwhite pixel count,
and a diagnostic FNV64 over the returned RGB pixels.

Crucial: **native C11 Raster is never invoked and no pixel-by-pixel
PDFium-vs-native comparison is attempted**. This is an external ORIGINAL
**device-extent agreement** oracle. It is separate from the #335
test-generated 8-page exact-RGB positive and from the source's strict
Content `UNSUPPORTED` test. We must not call it a full native pixel
positive, and do not require RGB hashes to match across operating
systems or imported PDFium versions.

The independent report is JSON, produced atomically, with a pinned
SHA-256 and immutable source byte length checked before and after.
All four CI configurations upload the new report:
Linux Release, Linux ASan/UBSan, Windows and macOS.

The source remains quarantined under CC-BY-SA-4.0, pending distinct
[Issue #336](https://github.com/qigao/QuantaPDF/issues/336) creator/
asset rights review, outside the official external corpus allowlist.
The original does **not** contain a /UserUnit key (default 1). The
requested extra-rotations, Image/SMask, Text/annotation and full C11
render compatibility remain unsupported; no production Graph limits,
native Raster, SDK/ABI, fallback or publication changes.

Keep [#269](https://github.com/qigao/QuantaPDF/issues/269)
OPEN, all PRs Draft / DO NOT MERGE, external native positive
increment 0, installed SDK PDFium and #208 cutover blocked.
