# QuantaPDF Composer Glyph-Run Design

**Date:** 2026-09-23  
**Tracking:** #81  
**API release:** 2.11.0

## Goal

Provide a stable backend-neutral surface for caller-shaped text without coupling
the QuantaPDF ABI or PDF backend to HarfBuzz, FreeType, CoreText, DirectWrite,
or another shaping implementation.

The layering is:

```
UTF-8 + script/language/direction
        ↓
caller-selected shaping engine
        ↓
quantapdf_composer_glyph[]
        ↓
quantapdf_composer_draw_glyph_run()
        ↓
Composer IR
        ↓
private Type0/CIDFont backend
```

The existing Base-14 and direct-cmap embedded-text APIs remain unchanged.

## Public glyph record

`quantapdf_composer_glyph` is a fixed-layout array element:

- `glyph_id`: font glyph ID supplied by the caller/shaper;
- `x_advance`, `y_advance`: pen advance in 1000/em units;
- `x_offset`, `y_offset`: glyph placement offset in 1000/em units;
- `unicode_offset`, `unicode_length`: byte range into the run's UTF-8 source
  buffer used only to build `ToUnicode`.

Because the type is traversed as a C array, 2.11 does not place
`struct_size` inside each element. A future incompatible element extension
requires a new type/API or an explicit-stride API.

The run options remain size-tagged and carry `font_id`, point size, and opaque
ARGB color.

## Geometry

The run origin is an explicit baseline in normal Composer displayed page space:

- PDF points;
- top-left origin;
- +x right;
- +y down.

Advances and offsets are normalized 1000/em values and are multiplied by
`font_size / 1000`.

The caller controls visual glyph order. QuantaPDF performs no bidi reordering.

## Unicode cluster mapping

The UTF-8 source is an explicit byte buffer and size, not a NUL-terminated text
contract.

Every nonempty glyph range must start and end on validated UTF-8 code-point
boundaries.

The mapping is deliberately per glyph:

- one glyph may map to multiple Unicode scalars, enabling ligatures or a shaped
  precomposed glyph to extract to its original source cluster;
- a secondary glyph in the same cluster may use `unicode_length = 0`, useful
  for combining marks that should render but must not duplicate extracted text;
- glyphs with zero-length mappings are omitted from `ToUnicode`.

## CID/GID separation

2.10 direct-cmap text uses glyph IDs directly as CIDs and keeps
`/CIDToGIDMap /Identity`.

Glyph-run resources are separate. QuantaPDF deterministically allocates one CID
per unique `(glyph_id, Unicode byte sequence)` pair in Composer operation
order.

Therefore the same GID may have multiple CIDs when it represents different
source clusters.

The glyph-run CIDFont contains:

- custom binary `CIDToGIDMap`;
- CID-specific `/W` widths;
- `ToUnicode` sequences from the caller's cluster mapping;
- the existing deterministic TrueType subset backend.

This avoids forcing text-extraction semantics into the font's original cmap.

## Painting

Glyph-run content uses the font size/color once and emits an explicit text matrix
for each positioned glyph. This is intentionally correctness-first; later
backend compaction may combine compatible placements without changing the ABI.

## Validation and ownership

Before publication QuantaPDF validates:

- existing page and `font_id`;
- finite origin, font size, advances, and offsets;
- opaque color;
- glyph IDs against the validated font `numGlyphs`;
- complete UTF-8 source validity;
- every cluster range and UTF-8 boundary;
- copied resource-byte limits and operation capacity.

The glyph array and UTF-8 source buffer are copied. Caller buffers are never
retained.

## Shaping boundary

This API is the shaping boundary, not a shaping implementation.

A later convenience integration may use HarfBuzz internally or externally, but
HarfBuzz handles/types must not leak into the stable C ABI and the Type0/CIDFont
backend must continue to accept explicit runs independently.

## Verification

Focused coverage includes:

- invalid glyph IDs, NaN/Inf placement, invalid UTF-8, and split code-point
  cluster ranges;
- kerning-like negative offsets;
- a single glyph mapping to a multi-scalar source cluster;
- a zero-advance/positioned secondary mark with no Unicode mapping;
- PDFium extraction through generated `ToUnicode`;
- PDFium rendering;
- content-position assertions;
- repeated-finish byte equality;
- static + ASan/UBSan CI;
- Windows installed-package compile/link/run coverage.
