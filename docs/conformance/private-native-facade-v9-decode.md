# Private Source V9 → built-in Page → Context-owned Bitmap

Tracks [#459](https://github.com/qigao/QuantaPDF/issues/459) and merged
Source V9 [#458](https://github.com/qigao/QuantaPDF/pull/458). This is
**private opt-in, test-only qualification**, not public rendering cutover.

## Architecture / ownership

- The existing product Native Source bridge now has an independent private
  `_v9` entry alongside the historically strict `_v1`/Source V8 entry.
  Both call **one** bounded bridge implementation, one existing pure-C11
  `quantapdf_ir_raster_scoped_mixed_v5` and one RGB conversion.
- Original immutable PDF bytes are borrowed from a real built-in Page; the
  selected Source converter creates uniquely owned, detached IR before
  Raster. A newly qualified original PDF is never passed to PDFium on a
  private-native failure or used to partially paint a bitmap.
- Page→backend RGBA→owned backend RGB→facade Bitmap conversion uses the
  existing private Context allocator and retained Context reference.
  On success the returned bitmap survives dropping the input PDF,
  Document, Page and caller's Context reference. On all errors the output
  resets, the temporary RGB/IR objects are disposed and references balance.
- Existing Source V8 APIs still **refuse any explicit Image /Decode**.
  The selected V9 entry admits only 8-bit DeviceGray/RGB exact endpoint
  [0 1]/[1 0], with per-component inversion; absent Decode is unchanged.
  Source, decoded sample, clip, draw and raster budgets are unaltered.

## Original-PDF conformance

`quantapdf.private_native_facade_bitmap_v9_decode` is a dedicated CTest
in the existing test-only QPDF-authored original/unchanged-PDFium oracle.
It opens actual facade documents and loaded pages and exercises the
**product-compiled private route**. It does not re-export Composer IR as
a source for the oracle.

Positive checks include explicit RGB full and selective inversion,
DeviceGray identity/inversion, implicit/explicit identity, ordered two/three/
four Path/Image layers, a split Contents W* with translated definition CTM,
and two distinct RGB /I and Gray /J XObjects with independent Decode.
Check whole-page RGB tolerance, fixed interior color probes, exact repeated
results, and Context/Bitmap dimensions, stride, components and lifetime.
V8/V9 byte equality is asserted on a shared implicitly identified original,
including after all original Page/Document/Context owners are dropped.
V8 must reject V9-only originals even if only the **second** image has
explicit Decode.

Negative checks cover malformed endpoint array on the second image
(after prior valid paints), near-binary endpoints, SMask, missing resource,
late Text, Source pixel/operation quota, Raster pixel quota and disallowed
device/page selections; every refusal resets the output and leaves Context
refs unchanged, with a valid post-refusal recovery. The existing one-shot
Context NOMEM injector runs on **valid V9** pages, with no retained bitmap.
The product-only static link canary also references the V9 symbols through
`QuantaPDF::QuantaPDF`, without inserting duplicate Raster objects.

## CI gate / exclusions

**Draft / DO NOT MERGE** until the exact HEAD passes full Linux Release
and ASan/UBSan, macOS, Windows, installed static/Windows DLL consumer,
Windows exact public ABI export checking, original unchanged-PDF pixel
oracle and strict licensed corpus reconciliation **7 original PDFs / 2
independent W* Native pixel positives**. Generated test originals do
not increase the licensed external corpus.

Public `quantapdf_render_page`, options, thumbnail and built-in backend
render callback remain **PDFium-backed**. The PDFium runtime/import/install/
package is unchanged; no public header/ABI export, C11 Raster cap increase,
font/text, Form, advanced Clip or transparency support is implied. Keep
#208/#145/#154/#214/#427 OPEN.
