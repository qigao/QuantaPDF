# Composer raw raster ingestion design

Tracks #94.

## Public API

```c
typedef enum quantapdf_composer_raster_format {
    QUANTAPDF_COMPOSER_RASTER_GRAY8 = 1,
    QUANTAPDF_COMPOSER_RASTER_RGB24 = 2,
    QUANTAPDF_COMPOSER_RASTER_RGBA32 = 3
} quantapdf_composer_raster_format;

typedef struct quantapdf_composer_raster {
    size_t struct_size;
    quantapdf_composer_raster_format format;
    uint32_t width;
    uint32_t height;
    size_t stride;
    const unsigned char *pixels;
    size_t size;
} quantapdf_composer_raster;

quantapdf_composer_add_raster(
    composer,
    &raster,
    &image_id);
```

The caller retains its source storage. QuantaPDF copies visible rows during the
call and retains no source pointer.

## Source validation

For a format with `source_components`:

```text
visible_row_bytes = width * source_components
required_source_size =
    (height - 1) * stride + visible_row_bytes
```

Every multiply/add is checked. Width and height must be nonzero, stride must be
at least the visible row width, and the supplied byte size must cover the final
visible source row. Trailing bytes and row padding are accepted and ignored.

## Canonical owned storage

```text
Gray8
  source stride rows
       ↓ copy visible bytes
  tightly packed DeviceGray bytes

RGB24
  source stride rows
       ↓ copy visible bytes
  tightly packed DeviceRGB bytes

RGBA32 (straight alpha)
  source stride rows
       ↓ split
  tightly packed DeviceRGB bytes
  + tightly packed 8-bit soft-mask bytes
```

RGBA32 is straight/unassociated alpha. The API does not claim premultiplied
input.

Only the canonical owned output bytes count against
`max_resource_bytes`; source padding does not.

## Backend

The existing Composer image state is reused. A private RAW image-state tag is
added only to distinguish ingestion provenance; qpdf lowering already handles
non-JPEG decoded image bytes as DeviceGray/DeviceRGB plus optional soft mask.

There is no new graphics operation and no second image backend. Registered raw
rasters return the same `quantapdf_composer_image_id` used by
`quantapdf_composer_draw_image()`.

## Failure and ownership

Validation/resource failures occur before allocation/publication. Allocation or
image-table growth failure releases all temporary canonical buffers. Output
image IDs reset to zero on failure.

## Direct PDF qualification

The focused test inspects generated image XObjects directly with qpdf in
addition to PDFium rendering. It requires Gray8 to publish `/DeviceGray`,
RGB24 to publish `/DeviceRGB`, and RGBA32 to publish `/DeviceRGB` with an
`/SMask`. This keeps backend qualification independent of reader-side decoded
component normalization.

## Qualification

- Gray8, RGB24, RGBA32;
- padded source stride;
- undersized source buffer and short stride;
- invalid format/zero dimensions/null pixels;
- checked overflow;
- canonical resource accounting independent of source padding;
- caller-source mutation after registration;
- RGBA soft-mask rendering;
- image extraction metadata;
- repeated-finish determinism;
- installed-package compile/run smoke.
