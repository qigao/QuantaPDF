# QuantaPDF OpenType CFF/CFF2 Composer Design

**Date:** 2026-09-23  
**Tracking:** #82  
**Feature release:** 2.12.0

## Goal

Extend the existing embedded-font APIs to OpenType fonts with Compact Font
Format outlines without introducing a second public font model.

The existing APIs remain:

```
quantapdf_composer_add_font()
quantapdf_composer_draw_embedded_text()
quantapdf_composer_draw_glyph_run()
```

No new public font handle or backend type is introduced.

## SFNT parsing

The private font parser recognizes:

- TrueType signatures `0x00010000` and `true` with `glyf/loca`;
- `OTTO` with exactly one of `CFF ` or `CFF2`.

All outline types share validation/parsing of:

- `head`;
- `hhea`;
- `hmtx`;
- `maxp`;
- `cmap`;
- optional `name`, `OS/2`, and `post`.

The CFF1 header is minimally validated as a version-1 CFF header. CFF2 is
validated as a version-2 header with a bounded Top DICT length. Full CFF
charstring interpretation is not needed by QuantaPDF because rendering remains
the responsibility of the embedded OpenType program in the PDF consumer.

## PDF representation

TrueType remains:

```
FontFile2
  ↓
CIDFontType2
  + CIDToGIDMap
  ↓
Type0 / Identity-H
```

OpenType CFF/CFF2 uses:

```
complete OpenType SFNT
  ↓
FontFile3 /Subtype /OpenType
  ↓
CIDFontType0
  ↓
Type0 / Identity-H
```

`CIDToGIDMap` is never written for `CIDFontType0`.

CFF/CFF2 embedding uses the complete original validated SFNT in 2.12. The
TrueType deterministic subsetter is not applied to CFF tables.

Because `FontFile3 /OpenType` is a PDF 1.6 feature, Composer output keeps its
existing PDF 1.4 minimum unless at least one actually referenced font resource
uses CFF/CFF2; only then is the writer minimum raised to 1.6.

## Direct-cmap text

`quantapdf_composer_draw_embedded_text()` keeps direct Unicode-cmap mapping.

For CFF-family resources the emitted CID is the font glyph ID. Widths are read
from the OpenType `hmtx` table and `ToUnicode` maps those CIDs back to source
Unicode.

Missing glyphs fail at draw time exactly as for TrueType.

## Glyph runs

PDF defines `CIDToGIDMap` only for Type2 CIDFonts. Therefore CFF/CFF2
glyph-run resources cannot use the 2.11 TrueType strategy of freely allocating
new CIDs for every `(GID, Unicode cluster)` pair.

For CFF-family runs:

- CID is fixed to GID;
- positioned advances/offsets remain fully supported;
- one GID may have one Unicode-cluster mapping in a Composer font resource;
- requesting the same GID with multiple distinct Unicode clusters fails
  `QUANTAPDF_ERROR_UNSUPPORTED`.

This is preferable to emitting a structurally invalid CIDFontType0.

## CFF2 qualification

CFF2 is newer than the CFF language described by older PDF references.
QuantaPDF does not claim CFF2 based only on parser acceptance.

2.12 qualification requires the real Source Sans 3 CFF2 variable-font fixture
to pass the same finish → reopen → PDFium extraction/rendering tests as CFF1.
If that gate fails on any supported platform, the release boundary is narrowed
to CFF1 and CFF2 remains tracked separately.

## Fixtures

- SourceSans3-Regular.otf — `CFF `;
- SourceSans3VF-Upright.otf — `CFF2`.

Both are copied byte-for-byte from Adobe's Source Sans repository and retain
their SIL Open Font License notice under test fixtures.

## Verification

Focused coverage requires:

- parser classification for CFF1 and CFF2;
- malformed CFF-header rejection;
- direct-cmap embedded text;
- positioned glyph-run rendering/extraction;
- PDF 1.6 output when CFF is referenced;
- repeated-finish byte equality;
- PDFium reopen/render/extract on real font programs;
- Linux sanitizer coverage;
- Windows/macOS full CI before merge;
- installed-package smoke using at least CFF1.
