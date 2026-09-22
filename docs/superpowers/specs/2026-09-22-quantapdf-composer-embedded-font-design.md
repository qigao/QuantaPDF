# QuantaPDF Composer Embedded Font Design

**Date:** 2026-09-22  
**Status:** Initial implementation for #72

## Goal

Add embedded-font text as an additive Composer capability without reinterpreting
the original Base-14 text API.

The stable public flow is:

```
font bytes
   ↓
quantapdf_composer_add_font()
   ↓
font_id
   ↓
quantapdf_composer_draw_embedded_text()
   ↓
Composer command IR
   ↓
private qpdf Type0/CIDFont backend
```

## Public ABI

`quantapdf_composer_font_id` is a nonzero 32-bit scalar.

`quantapdf_composer_add_font()` validates and copies a supported font before
publishing its ID. Byte-identical font data already registered in the same
Composer reuses the existing ID and does not consume the resource budget twice.

`quantapdf_composer_draw_embedded_text()` is separate from
`quantapdf_composer_draw_text()`. Its versioned option record carries
`font_id`, point size, opaque ARGB color, line-height multiplier, horizontal
alignment, and wrapping. This keeps the existing Base-14 ABI and behavior
unchanged while leaving room for later additive run/shaping options.

## Supported font boundary

2.10 accepts SFNT fonts with TrueType outlines:

- conventional TrueType (`0x00010000`) and Apple `true` signatures;
- required `head`, `hhea`, `hmtx`, `maxp`, `cmap`, `glyf`, and
  `loca` tables;
- Unicode cmap format 12 when available, otherwise format 4;
- optional `name`, `OS/2`, and `post` metrics.

This includes TTF and OpenType-TT fonts that use `glyf` outlines. CFF/CFF2-only
OTF is unsupported in this revision.

## Shaping boundary

Font embedding is not text shaping.

The V1 embedded-text path maps each Unicode scalar directly through the font
cmap. It does not apply GSUB/GPOS, bidi reordering, contextual shaping,
ligatures, or script-specific cluster formation. This is correct for simple
scripts and caller-prepared glyph-independent text; it must not be advertised
as general complex-script shaping.

Missing code points fail at draw time with `QUANTAPDF_ERROR_FORMAT`. No
replacement glyph is silently substituted.

A later shaping API can sit before a glyph-run emission surface without
changing the PDF font backend.

## PDF representation

Each used embedded font becomes:

```
FontFile2 (full original SFNT)
      ↓
FontDescriptor
      ↓
CIDFontType2
      ↓
Type0 / Identity-H
      + ToUnicode
```

The content stream encodes glyph IDs as 16-bit CIDs and uses
`/CIDToGIDMap /Identity`. `/W` contains widths only for glyphs actually
referenced by Composer text operations. `ToUnicode` maps those CIDs back to
the Unicode scalars observed in the source text, including UTF-16 surrogate
pairs for non-BMP values.

The full font is embedded in 2.10 rather than subsetted. This is intentionally
a correctness-first boundary; deterministic subsetting is a later optimization
and does not require a public ABI change.

## Ownership and limits

Font bytes and text are copied into Composer-owned storage and count against
the existing `max_resource_bytes` limit. Duplicate font registration is
deduplicated before accounting.

Validation happens before publication. Failed font or text mutations publish no
resource ID or command.

`finish()` reparses only Composer-owned validated bytes, remains
non-consuming, and must remain deterministic.

## Verification

Focused coverage includes:

- malformed and CFF-only font rejection;
- configured resource limits;
- duplicate font-ID reuse;
- missing glyph rejection at draw time;
- Unicode text outside WinAnsi;
- generated PDF reopen and text extraction through PDFium;
- real PDFium rendering of the embedded font;
- repeated-finish byte equality;
- ABI export/version coverage.

The test font is Apache-2.0 Roboto Regular and is stored only under test
fixtures with upstream provenance and license text.
