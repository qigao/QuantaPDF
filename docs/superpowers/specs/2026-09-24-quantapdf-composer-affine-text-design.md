# Composer affine text placement design

Tracks #92.

## Public affine value

```c
typedef struct quantapdf_affine_transform {
    float a;
    float b;
    float c;
    float d;
    float e;
    float f;
} quantapdf_affine_transform;
```

The transform is expressed in displayed page space (top-left origin, y down)
and maps a local point `(x, y)` to:

```text
X = a*x + c*y + e
Y = b*x + d*y + f
```

All six components must be finite. The value is a fixed mathematical record
like `quantapdf_point` and `quantapdf_rect`; it is not a versioned option
record.

## Additive drawing APIs

```c
quantapdf_composer_draw_text_transformed(...)
quantapdf_composer_draw_embedded_text_transformed(...)
quantapdf_composer_draw_glyph_run_transformed(...)
```

Existing draw APIs remain unchanged and lower through the same implementation
with the identity transform.

For text APIs, `bounds` stays in local displayed-page space. The #91 shared
layout core performs wrapping/alignment/vertical baseline admission in that
local rectangle first. The affine transform is then applied to each admitted
baseline origin.

For glyph runs, `origin`, advances, and offsets stay in the existing local
displayed-page convention. The completed local glyph placement is transformed
before PDF emission.

## PDF lowering

A local displayed-space text baseline point `(x, y)` first becomes:

```text
display_x = a*x + c*y + e
display_y = b*x + d*y + f
```

The corresponding PDF text matrix is:

```text
[a  -b  -c  d  display_x  page_height-display_y]
```

The sign changes convert the public y-down displayed coordinate system to PDF
y-up text space while preserving the affine effect on the glyph axes.

Identity therefore lowers to the existing byte shape:

```text
1 0 0 1 x page_height-y Tm
```

Zero-valued matrix terms are canonicalized so identity output does not regress
to `-0`.

## Semantics

- finite affine components only;
- layout metrics and wrapping remain unchanged by placement;
- arbitrary finite rotation, scale, reflection, and skew are representable;
- transformed text does not introduce clipping beyond the existing local
  vertical line-admission rule;
- embedded direct-cmap and caller-shaped glyph-run extraction remain driven by
  the existing ToUnicode paths;
- no shaping dependency is introduced;
- repeated finish remains deterministic.

## Qualification

- identity transformed API is byte-identical to the existing API;
- 90/180/270 degree Base-14 text;
- arbitrary finite rotation;
- rotated embedded Unicode text;
- transformed positioned glyph runs with advances/offsets;
- qpdf content-matrix evidence;
- PDFium rendering;
- text extraction;
- repeated-finish determinism;
- installed-package compile/run smoke.
