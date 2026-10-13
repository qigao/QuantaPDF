# #269 — Original jsPDF 1pt Stroke pixel-alignment A/B

Status: test-only, original-source SHA-pinned, no native pixel-positive
admission, no production C11 Raster change and no SDK backend switch.

## What is being distinguished

The genuine 3344-byte upstream jsPDF original has a horizontal black
1-point S stroke at source (x,y)=(50..250,821.89) PDF user coordinates.
Its displayed top-left Y is **20 PDF points**. The later W Clip affects
the red Fill only, not that earlier stroke. Probes at source X=200 and 220
are intentionally far from the red Fill boundary, joins and Butt Caps.

Three independent facts are measured, not silently treated as equivalent:

1. **PDF user→device coordinate API** via direct
   `FPDF_PageToDevice`/`FPDF_DeviceToPage`. These API operations map
   integer *device anchors*, not pixel antialiasing sample centers.
2. **Raw original PDFium raster RGB**, using
   `FPDFBitmap_Create`/`FPDF_RenderPageBitmap` with the same
   `FPDF_ANNOT | FPDF_LCD_TEXT` flags as the production aligned
   PDFium backend, on 72, 96 and 144 DPI aligned source rectangles.
3. **The actual private C11 NativeDeviceGrid 4×4 sample coordinates**,
   testing analytically whether they lie within the original horizontal
   1pt source-width stroke strip. Three *test-only* device-Y sampling
   phase hypotheses are evaluated: -0.5, 0, +0.5 device pixels.

No candidate phase is applied to NativeDeviceGrid or any pixel renderer.
The A/B error score is computed only on 2 interior columns × 7 nearby
scanlines × 3 DPI values (42 real-original PDFium observations), never
a substitute for the #281 full-image 501832-pixel oracle.

## Guardrails and provenance

The original source byte length and SHA-256 are checked both before and
after test execution. PDFium is invoked directly from test-only C11; no
QuantaPDF document renderer, qpdf-generated PDF or graph->PDF export is
used for the reference. This A/B program links only the pinned PDFium
and the **real private `native_device_grid.c` implementation**.

The CTest checker verifies all three device extents exactly, every source
X position, every sampled device index and RGB/alpha range, the actual
C11 unshifted phase, a numerical coordinate-API agreement threshold of
0.0005 source points, and a complete 42-observation report. It does **not**
claim independently measured PDFium AA sample centers.

Expected prior evidence: #282 measured 300/301 large delta pixels in
native Stroke-only partial coverage, worst (50,19) C11 white-composited
127 gray vs original PDFium full black. This new test independently probes
the unoccluded horizontal segment at x=200 and 220 to determine whether
the same row-coverage discrepancy persists at 72/96/144 DPI.

**Fail-closed boundaries:** original whole-page legacy and fill-only
entrypoints remain UNSUPPORTED/zero-pixels; the previously separate
Stroke/Fill and ordered candidate diagnostics remain unchanged. Original
positive counters and formal external W/W* corpus are not updated. Public
SDK continues using PDFium. Keep all stacked PRs Draft / DO NOT MERGE;
#269 remains Open.

Follow-up decisions must compare the A/B scores and actual PDFium scanline
RGB across platforms *before* changing the native line-alignment rule.
In particular, a device-pixel phase score improvement does not justify
blindly adding a +0.5 pixel translation to unrelated Path, Clip, W/W* or
UserUnit geometry.
