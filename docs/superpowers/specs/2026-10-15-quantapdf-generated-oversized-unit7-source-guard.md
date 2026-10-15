# #269 / #343 — Real test-generated oversized UserUnit7 Source-to-Graph capacity gate

**Test-only, private, Draft / DO NOT MERGE.** Unlike the synthetic
metadata refusal in #346, this fixture starts from genuine QPDF-generated
**ORIGINAL PDF bytes**, reopened and captured by the ordinary
Source-to-Graph path before device mapping. It is not the independently
posted Userunit.pdf attachment in dmester/pdftosvg.net #16: the attachment
bytes, SHA-256, source-specific redistribution rights and exact original
dictionaries are still unverified. This test makes no external provenance
or original-source pixel-compatibility claim.

## Isolation and ownership

QPDF creates four one-page sources with MediaBox [0 0 14300 500],
CropBox [10 20 14183.2 360.562], intrinsic Rotate 0/90/180/270,
leaf /UserUnit 7, and unpainted q Q content. CropBox/Rotate inheritance
alternates between the page tree and leaf. The source MediaBox remains
inside the 14,400-user-unit PDF limit; large displayed geometry comes
from /UserUnit instead.

Each source is reopened by quantapdf_open, captured with the unchanged
test-only Graph budget (10k nodes, 30k edges, 1MiB streams,
4MiB storage), and read with quantapdf_graph_query_page_geometry.
The document closes immediately after capture, and the Graph owner
drops after geometry checks. Generated source bytes are compared before
and after capture, and the temporary file uses its own name, preventing
parallel test races with the small Unit7 original pixel test.

## Bounded device grid

The visible crop measures 14,173.2 × 340.562 PDF user units. At UserUnit
7 its displayed extents are 99,212.4 × 2,383.934 page points.
Expected bitmap extents before swapping dimensions for 90/270 rotation:

| DPI | Width | Height | Pixels | RGB8 bytes |
|---|---:|---:|---:|---:|
| 72 | 99,213 | 2,384 | 236,523,792 | 709,571,376 |
| 96 | 132,284 | 3,179 | 420,530,836 | 1,261,592,508 |
| 144 | 198,425 | 4,768 | 946,090,400 | 2,838,271,200 |

The normal private **1M pixels / 3MB RGB8** cap must refuse all 12
rotation-by-DPI variants with a fully zeroed device grid.
Four additional byte-budget refusals exercise the independent cap.
At 72 DPI an explicitly larger **metadata-only** budget confirms exact
integer dimensions and one inverse/forward Q4 sample per rotation,
while eight source edges × 16 subpixel samples × pixel count must
refuse four times under the Q4 work budget. No huge bitmap allocation,
PDFium render or native raster is performed.

The isolated CTest emits an atomic JSON report, and CI publishes Linux
Release, Linux ASan/UBSan, Windows and macOS evidence. A missing,
partial, stale or malformed report is a test failure. This is a
**generated-original capacity negative**, not an externally published
original positive. Formal external native pixel count remains zero.

#269 / #343 stay OPEN. This branch stacks on Draft #347 and remains
Draft / DO NOT MERGE. No SDK/ABI, product renderer, fallback or
#208 backend cutover change; the installed/public SDK uses PDFium.
