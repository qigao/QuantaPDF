# QuantaPDF Composer Barcode Design

**Date:** 2026-09-22  
**Status:** Initial implementation for #70

## Goal

Generate machine-readable barcodes through the Composer vector-path layer added
by #69/#73. Barcode code must not emit PDF syntax or depend on qpdf/PDFium.

```
payload
  ↓
pure C encoder
  ↓
logical modules
  ↓
filled rectangle subpaths
  ↓
quantapdf_composer_draw_path()
  ↓
qpdf backend
```

## Initial formats

- Code 128B: printable ASCII, with mandatory start/check/stop symbols
- Code 39: base alphabet, no optional modulo-43 check character
- EAN-13: exactly 13 digits with a valid check digit
- UPC-A: exactly 12 digits with a valid check digit
- EAN-8: exactly 8 digits with a valid check digit
- QR: UTF-8 bytes in byte mode, error-correction level L, versions 1–5

PDFGen calls its printable-ASCII Code 128 implementation "Code-128A" while
using start symbol 104 (Code Set B). QuantaPDF exposes the correct Code 128B
name instead.

UPC-E is intentionally deferred inside #70 until its public input contract is
defined cleanly. PDFGen accepts an expanded 12-digit UPC-A representation,
which is not an appropriate public convention to copy accidentally.

## Geometry and quiet zones

The supplied bounds include the quiet zone.

One-dimensional symbols fill the bounds vertically and use these horizontal
quiet zones:

- Code 128B: 10 modules on both sides
- Code 39: 10 narrow modules on both sides
- EAN-13: 11 left, 7 right
- UPC-A: 9 left, 9 right
- EAN-8: 7 left, 7 right

QR uses four quiet modules on every side. The largest square fitting inside the
requested bounds is centered before module placement.

No human-readable caption is generated in this API. Text can be composed
separately with the existing text API.

## Painting

V1 accepts one opaque ARGB color and lowers all dark bars/modules into a single
fill-only vector path operation. This keeps barcode generation independent of
the private PDF backend and makes output resolution-independent.

## Limits and failures

- empty or malformed payload: `QUANTAPDF_ERROR_FORMAT`
- invalid options/kind/bounds: `QUANTAPDF_ERROR_ARGUMENT`
- QR beyond 106 UTF-8 bytes: `QUANTAPDF_ERROR_UNSUPPORTED`
- 1D payloads beyond 4096 bytes: `QUANTAPDF_ERROR_UNSUPPORTED`
- allocation failure: `QUANTAPDF_ERROR_NOMEM`
- Composer path/resource limits propagate from `draw_path`

## Verification

Focused tests cover format validation, strict EAN/UPC check digits, known
module geometry for Code 128B/EAN-13/QR, quiet-zone rendering through PDFium,
page reopen, and repeated-finish byte determinism.
