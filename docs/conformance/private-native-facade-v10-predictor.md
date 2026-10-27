# Private Source V10 → real Page/Context-owned Bitmap

Tracks [#464](https://github.com/qigao/QuantaPDF/issues/464),
merged [Source V10 #462](https://github.com/qigao/QuantaPDF/pull/462)
and [private Source V9 Bitmap #460](https://github.com/qigao/QuantaPDF/pull/460).

## Product architecture and strict profile selection

- The existing **one** product-native bridge now accepts a private
  typed Source selector, exclusively V8, V9 or V10. Its separate
  `_v1`, `_v9`, `_v10` entry points each pass a fixed selector.
  Unrecognized enum values are **UNSUPPORTED** and never silently
  downgrade Source grammar or retry PDFium.
- Every profile shares the same single Source→fully detached owned IR
  →pure-C11 scoped Raster→canonical backend RGB→Context-owned facade
  Bitmap pipeline and independently bounded Source/Raster work.
  V8 still refuses all explicit Image Decode; V9 admits only exact
  component identity/inversion; V10 additionally validates Flate TIFF/
  PNG Predictor 1/2/10..15 Image DecodeParms before decompression.
- The built-in provider supplies a borrowed immutable Page source view,
  with page index, password and fixed 72dpi device admission.
  The output Bitmap retains the actual Context once, and can be read
  using ordinary public bitmap dimensions/data/drop after dropping the
  input Page, Document, PDF bytes and caller's Context reference.
  Atomic failure publishes no bitmap, with balanced Context refs and
  no outstanding partially decoded RGB/IR resources.
- No public `quantapdf_render_page`, render options/thumbnail, C11
  raster, installed public header/export or PDFium runtime/import/
  package changes.

## Original-PDF conformance

New dedicated `quantapdf.private_native_facade_bitmap_v10_predictor`
CTest exercises the actual product-compiled private facade. QPDF authors
independently test-encoded predictor streams and preserves their exact
original Filter bytes with `setFilterOnWrite(false)`. Separate
**test-only direct PDFium** renders the unchanged original source;
Native converts only the Source-qualified RAW and uses the existing
C11 Raster and Context Bitmap allocator.

The fixture matrix includes RGB Predictor 1/2/10..15, Gray TIFF/PNG
plus exact Image Decode polarity, RGB independent R/B invert, 2/3/4
ordered Path/Image paints, frozen W/W* and real Q restore, translated
original W* split across Contents, and two actually distinct RGB /I
and Gray /J with independent predictor/Decode mappings.

Positive retained Bitmap tests compare independent original PDFium
full-page RGB tolerance and fixed interior RGB probes, and exercise
valid Context NOMEM fault injection and real Page/Document/Context
teardown before reading or dropping the Bitmap. With no explicit
DecodeParms, the **real V9 and V10 facade Bitmap bytes must be exact**
after all owners are destroyed; a V10-only original must be rejected
by the real private V9 facade, including an explicit Predictor only
on the second /J Image resource.

Repeated atomic refusals cover malformed second resource, nonmatching
Colors/Columns/depth, bad Predictor, mask/SMask, missing resource,
late Text, source and raster quotas, disallowed device/page, poison
output and Context reference balance. A valid V10 call after each
fault must recover correctly. A separate static product-only link
canary references all four private V10 symbols with no test-local
native Raster objects.

## Acceptance and exclusions

**Draft / DO NOT MERGE** until exact-head complete Linux Release
and ASan/UBSan, macOS, Windows CTest/Product Conformance, original-
source direct PDFium oracle, installed C11 SDK consumer, exact DLL
ABI baseline and external licensed original source reconciliation
**7 original PDFs / 2 independent W* Native pixel positives** qualify.
Generated predictor fixtures do not count as formal external originals.

This is private V10 only, not a public renderer cutover. Production
page/options/thumbnail remain PDFium-backed; #208/#145/#154/#214/#427
remain OPEN for the general source/render capability and complete
PDFium runtime/link/install/package exit.
