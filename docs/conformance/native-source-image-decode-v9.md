# Private Native Source V9 — 8-bit Image XObject /Decode

Tracks [#457](https://github.com/qigao/QuantaPDF/issues/457),
[#427](https://github.com/qigao/QuantaPDF/issues/427),
[#145](https://github.com/qigao/QuantaPDF/issues/145),
[#154](https://github.com/qigao/QuantaPDF/issues/154) and the
complete PDFium-free exit [#208](https://github.com/qigao/QuantaPDF/issues/208).

## Why this source milestone

PDF image streams carry sample bytes; the optional Image XObject
`/Decode` maps each color component from stored sample ranges into
its paint-space range. The merged Source V8 rejected **all** explicit
`/Decode`, including an exact identity [0 1] and an exact invert
[1 0]. That refusal correctly prevented painting unqualified source
colors as raw bytes, but excluded real PDFs that use explicit values.

The new **private, opt-in**
`quantapdf_qpdf_source_scoped_mixed_v9_image_decode_to_ir()` qualifies
only a strictly bounded subset: the existing 8-bit DeviceGray and
DeviceRGB, with either explicit [0 1] identity or [1 0] inversion
**per component**. PDF 32000-1:2008 section 8.9 describes this
linear mapping and explicitly demonstrates DeviceGray [1 0].

## Exact admission

- An absent `/Decode` retains the existing implicit identity.
  An explicit `/Decode` must be an array of **2 × 1** entries for
  DeviceGray or **2 × 3** entries for DeviceRGB. Every value must
  be numeric, finite and exactly zero or one; each component must
  specify exactly [0 1] or [1 0].
- By PDF dictionary semantics, a `null` value is equivalent to an
  omitted key (QPDF also treats it as absent), not a malformed present
  array. Numeric endpoint spellings `0.0`/`1.0` remain exact.
- Sample normalization uses exact 8-bit integer arithmetic:
  identity passes through, inversion is `255 - raw_sample`.
  Different RGB components may choose independent identity/invert
  pairs. This runs **after bounded lossless RAW/Flate stream decode**,
  before any Image RAW resource is materialized into uniquely owned
  Composer IR and before any pure-C11 Native rasterization.
- Arbitrary ramps such as [0 0.5], nonbinary or nonfinite values,
  malformed/short/long arrays, ImageMask, Mask/SMask/Matte, ICC/CMYK,
  DecodeParms, interpolation or unqualified stream filters remain
  **atomic UNSUPPORTED**, with no partial output and no PDFium fallback.
  All image/Content/ancestor/render quotas remain fully charged.
- The existing immutable Source profile table has one new V9 row,
  with a separate `explicit_image_decode` policy. **V1–V8 remain
  strict and refuse every explicit `/Decode`.** Both V8 and V9
  accept formerly qualified implicit-identity sources with **exact
  byte-identical raster output**. V9 does not implicitly replace the
  production/private Source V8 page bridge.
- Page selection, 72-DPI, frozen one W or W* Clip, real q/Q restore,
  2..4 mixed source-ordered Path/Image operations, 1..2 of each kind,
  Source V7 normalized `rg` and V8 normalized `g` Path fill, owned
  IR lifetime, and unchanged pure-C11 V5 Raster remain in force.

## Original PDF test-only oracle

The new CTest `quantapdf.native_source_scoped_mixed_v9_decode` uses
the actual QPDF fixture writer and embeds explicit `/Decode`
dictionaries in the **unchanged original PDF** sent to PDFium.

Its direct-original PDFium and backend-free Native pixel comparisons
include: all 8 independent RGB inversion bitmasks, numeric real-form
identity; DeviceGray inverted and identity; source-order 2, 3,
4 mixed Path/Image cases; W* with definition-time translation and
Contents split before `n`; real Q restoration; whole-page MAE and
gross-error thresholds unchanged from V6–V8; exact fixed interior
red/cyan/blue/yellow/gray/white probes.

A separate original-PDF fixture uses **two distinct XObject resources**
(`/I` RGB and `/J` Gray), with independent per-resource Decode pairs
and source-ordered clipping/Q restore. It verifies both an explicit
RGB invert with a Gray identity and an implicit RGB identity with a
Gray invert, plus a malformed **second** Decode after earlier admitted
paints (all-or-nothing output zero).

Both old V8 on exact implicit identity and new V9 on explicit identity
must yield **byte-identical** Native RGBA buffers. New V9-positive
sources must still be rejected atomically by V8. Negative tests cover
wrong array type/length, constant or arbitrary endpoint, bad ranges,
nonnumeric endpoints, late unsupported Text, masked/missing resources,
Source pixel/operation budget exhaustion and valid recovery.

## Qualification and exclusions

**Draft only** until this exact head passes Linux Release + ASan/UBSan,
macOS, Windows, original unchanged-PDF direct PDFium pixel comparison,
installed C11 SDK and Windows DLL public ABI exports, and strict
external licensed original PDF corpus reconciliation. Formal corpus
remains **7 originals / 2 independent W* Native pixel positives**:
generated Decode fixture PDFs do not increase this count.

This does **not** introduce new C11 renderer objects, public ABI,
general Decode ramps, ICC/calibrated/CMYK, JPEG/predictors, alternate
ColorSpaces, transparency/forms/text/font or any PDFium production
backend change. The public page/options/thumbnail renderer and
installed PDFium runtime/import/package remain untouched. Keep
#427/#145/#154/#214/#208 OPEN.
