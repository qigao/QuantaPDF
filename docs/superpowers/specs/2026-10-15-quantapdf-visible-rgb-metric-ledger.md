# #269 — Test-generated original PDF RGB ledger (observation only)

Stacked exactly on #333, an already-validated full three-platform test that
creates eight real, **test-owned** source PDFs with nonzero /CropBox,
intrinsic /Rotate 0/90/180/270, /UserUnit 1 or 2, a nonzero W Clip and
opaque Fill. The original bytes are separately rendered by test-only
PDFium and compared against source → sealed Graph → **detached owned** IR
→ private pure-C11 visible-device raster.

## The missing evidence

The previous `quantapdf.native_generated_visible_page_pixels` CTest passed
on Linux, macOS and Windows but only printed error measurements to its
stderr. CTest normally hides output from passing tests; the successful
job logs therefore showed **no page-by-page RGB values**. A passing coarse
error threshold is not evidence for zero-difference parity.

## New strict report gate

The independent test-only CMake wrapper now executes the unchanged
original-source oracle a second time with an explicit 30-second cap,
requires exactly eight original-page error records in canonical
(rotate,UserUnit) order, verifies pixel counts and error bounds, and
materializes an atomic JSON report per build configuration.

The report records per-page original PDFium vs C11:
- source rotation, UserUnit and pixel count;
- summed absolute RGB channel error, differing pixels, >64-delta pixels,
  maximum channel delta and mean absolute channel error;
- aggregates across the eight generated source pages.

Both the JSON payload and CTest results explicitly mark these as
**observation only**, `pixel_parity_qualified=false`, with
`external_source_positive_increment=0`. Existing 14-family/36-fixture
source manifests and independently licensed external PDFs remain separate.

The normal CI uploads independent per-platform JSON artifacts:
Linux Release and Linux ASan/UBSan, macOS, and Windows. Missing reports
or a failed original-source oracle fail the workflow rather than publishing
stale or partial metrics. The RGBA renderer, source PDF content, SDK,
formal external counts and the original jsPDF exact-pixel sentinel remain
unchanged.

## Follow-on

1. Verify four original JSON files and compare eight per-case values
   across all platforms.
2. Classify any differences by page edge, W Clip intersection and
   oversampled Fill mask; do not claim zero-RGB parity until the data
   genuinely support it.
3. Independently license-review and hash-pin additional externally
   published source PDFs. The upstream py-pdf/sample-files CC-BY-SA-4.0
   cropped/rotated/scaled fixture is presently only a **discovery
   candidate**, not covered by the existing manifest's license allowlist
   and not qualified for the Fill/W Clip native-positive subset.
4. Keep #269 open, every stacked PR Draft / DO NOT MERGE, installed SDK
   PDFium and #208 backend cutover blocked.
