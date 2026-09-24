# Composer text measurement design

Tracks #91.

## Contract

Add two non-mutating Composer queries:

```c
quantapdf_composer_measure_text(...)
quantapdf_composer_measure_embedded_text(...)
```

Both return the same size-tagged result:

```c
typedef struct quantapdf_composer_text_measurement {
    size_t struct_size;
    float width;
    float height;
    size_t line_count;
} quantapdf_composer_text_measurement;
```

`max_width` is finite and positive. Measurement validates the same text/font
options and text representability as the corresponding draw call. It adds no
Composer operation and retains no caller pointer.

For a laid-out result with `N >= 1` lines:

```text
width  = max(line_width[i])
height = font_size + (N - 1) * font_size * line_height_multiplier
```

The height therefore matches the current Composer baseline/clipping model: a
box of exactly that height can publish every measured baseline.

## Architecture

```text
UTF-8 + options + max_width
          |
          v
composer_text_layout
  |                 |
  | Base-14         | embedded SFNT
  v                 v
line snapshots with exact widths
          |
    +-----+------+
    |            |
 measure()     qpdf draw
```

The layout module is backend-neutral: it depends on Base-14 metrics and the
private SFNT face parser, but not on qpdf/PDFium. qpdf consumes the same line
snapshots used by measurement, so wrapping, whitespace trimming, tabs and
missing-glyph behavior cannot drift into a second algorithm.

## Compatibility

Existing draw APIs and operation records are unchanged. The change is additive
under ABI v2 and increments the package version to 2.14.0.
