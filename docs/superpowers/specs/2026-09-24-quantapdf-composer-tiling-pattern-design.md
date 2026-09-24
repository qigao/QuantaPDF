# Composer tiling-pattern paint design

Tracks #118 under #116.

## Goal

Add reusable colored tiling patterns as another implementation of the existing
generic `quantapdf_composer_paint_id`.

PATH does not gain another ABI tail. Fill/stroke consumers continue using the
V3/V4 paint IDs introduced for gradient paint.

## Public API

```c
typedef quantapdf_status (*quantapdf_composer_pattern_builder_fn)(
    quantapdf_composer *tile_composer,
    size_t page_index,
    void *user_data);

typedef struct quantapdf_composer_tiling_pattern_options {
    size_t struct_size;
    float width_points;
    float height_points;
    float x_step;
    float y_step;
    quantapdf_affine_transform transform;
} quantapdf_composer_tiling_pattern_options;

quantapdf_composer_add_tiling_pattern(
    composer,
    options,
    builder,
    user_data,
    &paint_id);
```

Validation:

- width/height finite and strictly positive;
- x_step/y_step finite and nonzero;
- all-zero transform record means identity;
- other transforms finite and non-singular.

The callback/user pointer is synchronous and never retained.

## Snapshot model

The builder receives a temporary transparent one-page child Composer sized to
the tile width/height. It may use the ordinary Composer primitives, including
text, images, gradient paint, graphics state, Forms and nested tiling patterns.

Navigation/outlines are not valid tile content.

After successful builder return:

1. compose the child deterministically;
2. drop the child Composer;
3. deduplicate by tile size, x/y step, normalized transform and exact PDF bytes;
4. charge only the stored snapshot bytes to the parent resource budget;
5. store the snapshot in the existing private paint table.

The paint kind distinguishes linear gradient, radial gradient and tiling pattern
ownership. Gradient paints own stop arrays; tiling paints own PDF snapshot
bytes.

## PDF PatternType 1 lowering

At parent finish, a referenced tile snapshot is parsed and converted through
the existing qpdf page-to-Form helper. The source QPDF remains alive until the
parent writer finishes, matching the reusable Form lifetime rule.

Each page that references the paint receives a colored PatternType 1 stream:

```text
/Type /Pattern
/PatternType 1
/PaintType 1
/TilingType 1
/BBox [0 0 width height]
/XStep x_step
/YStep -y_step
/Resources << /XObject << /Tile <form> >> >>
```

The pattern stream is simply:

```text
q /Tile Do Q
```

Positive public y_step means downward repetition. Native PDF pattern
coordinates are y-up, hence the negative /YStep.

## Pattern matrix

The imported tile Form already uses native PDF y-up coordinates for a local
displayed-space tile of height Ht.

For page height Hp and public displayed transform:

```text
X = a*x + c*y + e
Y = b*x + d*y + f
```

the page-specific Pattern matrix is:

```text
[a, -b, -c, d,
 c*Ht + e,
 Hp - d*Ht - f]
```

This is the same Form-placement conjugation used by reusable Form XObjects.

## PDF version propagation

If a referenced tile snapshot requires PDF 1.6 (for example embedded CFF/CFF2
content), the parent output is also raised to PDF 1.6. Unreferenced paint
resources do not affect the output version.

## Compatibility

No existing PATH option, draw call or paint ID contract changes.

Gradient PatternType 2 lowering is unchanged.

## Qualification

- argument/step/transform validation;
- builder error and extra-page rejection;
- exact snapshot+geometry deduplication;
- resource-budget failure;
- direct PatternType/PaintType/BBox/XStep/YStep/Matrix inspection;
- imported /Tile Form evidence;
- fill and stroke consumption through existing paint IDs;
- translated and rotated pattern transforms;
- repeated-finish determinism;
- PDFium repeated-cell rendering;
- installed-package smoke;
- Linux/macOS/Windows full CI.
