# #269: original jsPDF direct PDFium coordinate oracle

This test-only stage follows #272's planner contracts. It does not implement
native fractional-page rendering or change #270's zero-output UNSUPPORTED gate.

## Independent input and observations

Use the existing, unchanged `jspdf.w3s-clip-v1.pdf`: 3,344 bytes, SHA-256
`179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e`.
The CMake guard checks size and SHA before execution and SHA again afterwards.
It refuses source symlinks and captures only process stdout; there is no report
output path that could alias the source and no stale JSON file to reuse.

`test_original_jspdf_coordinates.c` directly links the existing pinned PDFium
target and includes its actual `fpdfview.h` in repository CI. It does not link
QuantaPDF's planner, backend wrapper or native renderer. The fixture is loaded
read-only; there is no generated or re-exported PDF, mutation or bitmap render.

Eighteen fixed display rectangles cover 72/96/144-DPI-sized extents, PDFium
rectangle orientations 0/1/2/3, and translated rectangles associated with
positive and negative fractional render-clip origins. The full-page draw
extent remains distinct from the output viewport used to select probe points.
These are coordinate checks, not evidence that clipping pixels were rendered.

Each rectangle has nine integer device anchors, repeated three times: **486
checks**, each requiring DeviceToPage success, independent analytical agreement
within 0.0005 PDF points, PageToDevice success and exact integer round-trip.
Repeated returned coordinates must be numerically identical. The tolerance
accommodates the float-backed API's arithmetic, not a pixel-difference budget.
The report includes observed provider dimensions, maximum coordinate error,
and measured local inverse-axis differences at the 72-DPI origin. The latter
must reject the assumption that one device coordinate unit equals one point.

## Evidence boundaries

PDFium's `rotate` rectangle parameter is NOT QuantaPDF's requested additional
rotation path, which uses a separate matrix rendering path. Original raw
CropBox-offset, intrinsic Rotate and UserUnit variants remain unqualified here.
The unchanged fixture only establishes its own zero-origin page geometry.

DeviceToPage accepts integer device coordinates. No half-pixel or antialiasing
sample-center observation is claimed; `pixel_center_observations=0` explicitly
preserves that distinction. `raster_invocations=0` and
`public_requested_rotation_qualified=false` are checked by the CMake guard.
The coordinate summary is captured in the CTest log, not a new uploaded CI
artifact. This adds one CTest, not 486 CTests and not a formal corpus entry.

Local smoke tests may use a different installed PDFium and reduced declaration
facade; those are not pinned-SDK or full-repository validation. Acceptance
requires the new exact HEAD under the existing full-ci workflow and actual
pinned headers on Linux Release + ASan/UBSan, Windows and macOS. An instrumented
test executable does not imply the prebuilt PDFium library is instrumented.

Next: qualify the wrapper's actual matrix path and geometry precision policy,
then implement bounded native mapping and compare whole images without changing
original bytes. Native pixel, output-byte, edge and sample-work budgets remain
separate gates. No corpus promotion, W* increment, fallback or SDK cutover.
#269 remains Open; stacked PRs stay Draft / DO NOT MERGE; public SDK stays PDFium.
