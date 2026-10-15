# #269 — Source-backed Unit7 72/96/144 DPI RGB diagnostic matrix

**Private test-only; Draft / DO NOT MERGE.** The parent Draft #348 established
Source→bounded Graph page-geometry refusal for a **test-generated** oversized
Unit7 PDF. The older Draft #347 established zero-RGB PDFium-versus-C11
parity for four small **test-generated** Unit7 originals at **72 DPI only**.

This child retains exactly that small original W Clip + opaque Fill source
syntax, with inherited/leaf CropBox and intrinsic rotations 0/90/180/270.
The QPDF fixture writer is **not a separately published third-party PDF
producer**. The binary source is opened through the real Graph→detached Owned
Composer IR pipeline and destroyed before private C11 painting; the
PDFium reference independently reopens the original bytes, never a
Composer re-export.

## DPI and geometry

Visible CropBox [2 3 12 11], MediaBox [0 0 20 30], leaf /UserUnit 7.
Exact expected bitmap widths/heights, before 90/270 quarter-turn swap:

| DPI | Width | Height | Pixels/page |
|---|---:|---:|---:|
| 72 | 70 | 56 | 3,920 |
| 96 | 94 | 75 | 7,050 |
| 144 | 140 | 112 | 15,680 |

Total 4 rotations × 3 resolutions: 12 original-source bitmap comparisons,
106,600 observed pixels. 96 DPI deliberately includes fractional bitmap-fit
scales: 70×4/3=93.333... rounds to 94 device pixels; 56×4/3=74.666...
rounds to 75. This isolates pixel-center/AA differences from the
correctness of page-to-device integer extents.

## Explicit result interpretation

* **72 DPI**: strict 4/4 full-page zero RGB error, the already qualified
  baseline; a single channel difference is fatal.
* **96/144 DPI**: full-page PDFium-versus-C11 RGB absolute error, changed
  pixels, gross deltas and maximum channel delta are measured and
  independently reported. They are **diagnostics**, not automatic new
  pixel positives, even if a CI run is green.
* All 12 modes require actual original PDFium dimensions equal the C11
  device grid, repeat-identical native pixels and repeat-identical
  original PDFium bytes, independent painted red/white golden samples,
  and completely unchanged borrowed Path and Clip commands.
* Three fail-closed zero-output negative controls per mode: wrong
  UserUnit=1 projection, one pixel under the required budget, and one
  RGBA byte under the required capacity; 36 negative checks total.
* Original generated source bytes are checked byte-for-byte after each
  rotation. No additional Source-to-Device affine may be applied
  after Graph→Composer lowering.

The isolated CTest's atomic JSON and platform artifacts must be inspected
for actual 96/144 DPI errors before promoting any exact-parity claim.
An unqualified difference is **not** silently averaged into success:
the raw metrics are separately published and present in each platform
job log. Fixes must be separate A/B experiments, not hidden fallback.

No production Graph/Raster algorithm, public ABI, SDK, external source
manifest or formal original-positive count changes. The independently
published nondefault Unit7 reporter sample remains unverified by
bytes and redistribution rights (#343), and a separately published,
rights-clear, small painted Fill/W Clip nondefault-Unit source is still
needed for multi-producer native pixel acceptance.

Keep #269, #343 OPEN; this and parent PRs **Draft / DO NOT MERGE**.
Installed/public SDK PDFium; #208 backend cutover blocked.
