# Composer graphics-state design

Tracks #100 under #99.

## Goal

Add reusable opacity and blend-mode state without creating a parallel family of
draw entry points and without changing the behavior or byte shape of existing
V1 callers.

## Public resource

```c
typedef uint32_t quantapdf_composer_graphics_state_id;

typedef enum quantapdf_composer_blend_mode {
    QUANTAPDF_COMPOSER_BLEND_NORMAL = 0,
    QUANTAPDF_COMPOSER_BLEND_MULTIPLY = 1,
    QUANTAPDF_COMPOSER_BLEND_SCREEN = 2,
    QUANTAPDF_COMPOSER_BLEND_OVERLAY = 3,
    QUANTAPDF_COMPOSER_BLEND_DARKEN = 4,
    QUANTAPDF_COMPOSER_BLEND_LIGHTEN = 5
} quantapdf_composer_blend_mode;

typedef struct quantapdf_composer_graphics_state_options {
    size_t struct_size;
    float fill_alpha;
    float stroke_alpha;
    quantapdf_composer_blend_mode blend_mode;
} quantapdf_composer_graphics_state_options;
```

`quantapdf_composer_add_graphics_state()` validates and copies the fixed value,
deduplicates identical tuples, and returns a nonzero immutable ID.

Alpha values are finite in [0, 1]. Negative zero is canonicalized to positive
zero before deduplication.

## Operation attachment

Existing size-tagged draw options gain a V2 tail:

```c
quantapdf_composer_graphics_state_id graphics_state_id;
```

for:

- Base-14 text options;
- embedded-text options;
- glyph-run options;
- image options;
- path options.

All V1 SIZE macros remain the size of the pre-2.18 layout. V2 MIN/SIZE macros
cover the new tail. A caller supplying only V1 bytes gets state ID 0.

State ID 0 is the implicit legacy state:

```text
fill alpha   = 1
stroke alpha = 1
blend        = Normal
```

No ExtGState resource or `gs` operator is emitted for ID 0, preserving
existing output.

Measurement APIs ignore the graphics-state tail because compositing does not
affect layout.

## ABI-safe publication

After appending fields, production code must not assign a caller option record
with `stored = *options`: an older binary can legitimately pass a smaller V1
record.

Each publishing path instead:

1. validates only fields covered by V1;
2. reads `graphics_state_id` only when
   `struct_size >= *_V2_MIN_SIZE`;
3. validates that the ID is 0 or references a registered state;
4. copies V1 fields explicitly into zero-initialized owned operation state;
5. stores the normalized state ID in the common operation record.

This applies to text, embedded text, glyph runs, images, and paths.

## Private IR

```text
operation
  kind
  page
  bounds
  graphics_state_id   // 0 = legacy default
  value{...}
```

Graphics-state resources live in a Composer-owned fixed-record array.

## PDF lowering

Each registered state lowers to one deduplicated ExtGState resource:

```text
<<
  /Type /ExtGState
  /ca fill_alpha
  /CA stroke_alpha
  /BM /Normal|/Multiply|/Screen|/Overlay|/Darken|/Lighten
>>
```

Only states referenced by a page are installed into that page's
`/ExtGState` resource dictionary as `/GS<n>`.

For a nonzero operation state, page-content emission wraps the existing
operation:

```text
q /GS<n> gs
  <existing operation bytes>
Q
```

This prevents state leakage. Existing path/image internal q/Q pairs may nest
safely.

PDF 1.4 already covers the required transparency/blend model, so no version
raise beyond the current minimum is required.

## Qualification

- validation of NaN/out-of-range alpha and invalid blend mode;
- equal-state deduplication;
- V1 options remain byte-identical to pre-feature output;
- V2 state ID 0 matches V1 output;
- invalid state IDs reject publication;
- text, embedded text, glyph run, image, solid path, dashed path attachment;
- exact ExtGState /ca /CA /BM evidence;
- balanced q/gs/Q isolation;
- PDFium opacity/blend rendering;
- RGBA-image + external opacity interaction;
- deterministic repeated finish;
- Windows installed-package consumer.
