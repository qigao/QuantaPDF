# QuantaPDF Deterministic TrueType Subsetting

**Date:** 2026-09-22  
**Tracking:** #80

## Goal

Reduce the size of PDFs produced by the 2.10 embedded TrueType Composer path
without changing any public ABI or text semantics.

Subsetting is a private `finish()` optimization. The Composer continues to
own the complete validated source font for its full lifetime.

## Key design choice: preserve glyph IDs

The subset keeps the original glyph IDs rather than renumbering them.

That means the existing:

- content-stream CIDs;
- `/CIDToGIDMap /Identity`;
- `/W` entries;
- `ToUnicode` mappings;

remain unchanged.

Unused glyph IDs below the highest retained glyph remain present as zero-length
glyphs. This sacrifices some theoretical minimum size in exchange for a much
smaller correctness surface and zero changes to the 2.10 text pipeline.

## Glyph closure

The subset always retains glyph 0 plus every glyph directly referenced by
Composer text.

For composite TrueType glyphs, the subset walks component records recursively
and retains the full transitive component closure.

The walk validates:

- component glyph indices;
- record bounds;
- scale/transform payload sizes;
- optional instruction bounds;
- recursion depth;
- component cycles.

If this private optimization cannot prove the subset safe, Composer falls back
to embedding the original complete font rather than changing previously valid
behavior.

## Rebuilt tables

The subset deterministically rebuilds:

- `glyf`;
- long-format `loca`;
- `hmtx` with one full metric per retained glyph index;
- `cmap` from Unicode scalars actually used by the document;
- `head` with long `loca` and recalculated checksum adjustment;
- `hhea` metric count;
- `maxp` glyph count;
- `post` as format 3.0.

Safe global tables such as `OS/2`, `name`, `cvt `, `fpgm`, `prep`,
and `gasp` are copied when present.

Layout, color, variation, and other glyph-index-bearing tables are deliberately
not copied because the current Composer text path does not consume shaping,
variation, or color-font semantics.

## cmap

A Windows Unicode format-12 cmap is always emitted for the used Unicode set.
A format-4 Windows BMP cmap is also emitted when its deterministic one-segment-
per-codepoint representation fits the format-4 16-bit length limits.

Glyph IDs in the cmap stay identical to the original font.

## SFNT integrity

Tables are emitted in deterministic tag order with four-byte alignment.
Checksums and the `head.checkSumAdjustment` value are regenerated. The final
font checksum must equal the TrueType magic checksum `0xB1B0AFBA`.

## Fallback rule

The generated subset is used only when:

1. subsetting succeeds; and
2. the subset bytes are strictly smaller than the original source font.

Otherwise the existing full-font embedding path is used.

## Verification

- existing embedded-font PDFium rendering and extraction must remain green;
- `Café Ω` exercises non-ASCII and composite glyph closure;
- repeated `finish()` remains byte-identical;
- output PDF must be smaller than the original Roboto test TTF;
- Linux ASan/UBSan must remain green;
- the installed-package Composer smoke must continue to pass.
