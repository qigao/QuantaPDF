# Private Native Source V10 — bounded 8-bit Flate predictors

Tracks [Issue #461](https://github.com/qigao/QuantaPDF/issues/461),
merged Source V9 [#458](https://github.com/qigao/QuantaPDF/pull/458),
private Page/Context V9 [#460](https://github.com/qigao/QuantaPDF/pull/460),
and #427/#145/#154/#208.

## Admission boundary

QPDF's existing Flate stream pipeline implements TIFF and PNG predictor
reconstruction. The new **private, independently selected** Source V10
profile does **not** duplicate this decoder or modify C11 Raster.

- Source V1–V9 still refuse *every* explicit Image `/DecodeParms` before
  decompression. V10 accepts only named `/Filter /FlateDecode` (no arrays
  or chains), plus one direct `/DecodeParms` dictionary with explicit
  integer `/Predictor`, `/Colors`, `/Columns` and
  `/BitsPerComponent`; no extra keys.
- Exact Predictor values: 1 (none), 2 (TIFF horizontal), 10..14 (PNG
  None/Sub/Up/Average/Paeth), 15 (PNG adaptive). Each PNG row carries
  one filter-type byte consumed by the existing QPDF decoder.
- Only 8-bit DeviceGray or DeviceRGB; `Colors` exactly 1/3 according to
  ColorSpace, `Columns` exactly image Width and `BitsPerComponent=8`.
  Width/Height, source-pixel/operation budgets and row-size bounds are
  verified BEFORE any decoder/predictor buffers can be built. The stream
  sink never accepts more than `Width*Height*channels` reconstructed
  bytes, and exact output size is mandatory.
- Original Source V9's per-channel exact Image `/Decode` identity or
  inversion is applied **after** predictor reconstruction. One frozen
  W/W* Clip, true Q restore, 2..4 ordered Path/Image operations, owned
  detached RAW, C11 V5 Raster and all independent quotas are unchanged.
- Deliberate refusals: other predictors, filter arrays/chains, missing
  or defaulted metadata, array/non-dictionary DecodeParms, mismatched
  row size/channel/depth, masks, ICC/CMYK, general Decode ramps,
  unqualified Text/Form, transparency and unknown keys. PDF `null`
  retains normal missing-key semantics; missing DecodeParms continues
  to admit previously supported Flate sources.

## Independent original-PDF qualification

`quantapdf.native_source_scoped_mixed_v10_predictor` creates actual
QPDF-authored **unchanged** original PDFs with test-only zlib-compressed
predictor-encoded two-row Gray and RGB sample bytes. Direct test-only
PDFium independently paints the original source. Native Source V10
builds detached owned IR, destroys the source owner and uses the
existing PDFium-free C11 Raster V5.

Positive coverage: TIFF-2 and PNG 10..15 (all 0..4 row filter methods
plus mixed adaptive Sub/Paeth), explicit Predictor 1, Gray inversion,
RGB independent per-channel inversion, 2/3/4 Path/Image source order,
two different Image XObjects (/I RGB adaptive and /J Gray TIFF), and
translated original W* split across Contents with Q restoration.
Old Source V9 must refuse each explicit V10 predictor dictionary.

Negative tests include non-dictionary, missing/wrong/extra keys,
noninteger/out-of-range Predictor, Colors/Columns/bit-depth mismatch,
huge requested row size, raw image + DecodeParms, masked resource,
late Text, missing XObject, source/operation exhaustion and malformed
second resource after earlier accepted paints. Every refusal resets
poisoned IR output; valid conversion must succeed afterward.

## Merge gate and exclusions

**Draft / DO NOT MERGE** until exact-head Linux Release, ASan/UBSan,
macOS and Windows complete CTest/Product Conformance, independent
original-source direct PDFium pixels/goldens, installed C11 consumer,
Windows DLL ABI baseline and strict licensed original source corpus
**7 originals / 2 independent W* Native pixel positives** pass.
Generated Predictor originals are *not* licensed corpus additions.

V10 remains a **private Source-only opt-in**: merged product-owned
Page/Context/Bitmap still selects V9, and all public page/options/
thumbnail rendering remains PDFium-backed. No C11 Raster expansion,
public ABI, runtime/link/install/package PDFium removal, or full #208
cutover is included.
