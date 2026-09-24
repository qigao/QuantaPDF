# Composer clip-path resource design

Tracks #102 under #99.

## Public resource

```c
typedef size_t quantapdf_composer_clip_id;

typedef struct quantapdf_composer_clip_options {
    size_t struct_size;
    quantapdf_composer_fill_rule fill_rule;
    quantapdf_affine_transform transform;
} quantapdf_composer_clip_options;

quantapdf_composer_add_clip_path(
    composer,
    commands,
    command_count,
    options,
    &clip_id);
```

The Composer copies generic path commands during registration. The all-zero
transform record means identity; any explicit transform must be finite and
non-singular.

## Graphics-state V2

The existing immutable graphics-state record gains a V2 tail:

```c
quantapdf_composer_clip_id clip_id;
```

V1 size remains the pre-2.20 alpha/blend layout. V1 and V2 with clip ID 0 are
semantically identical and deduplicate to one graphics-state ID.

A clip-only state is fill/stroke alpha 1, Normal blend, and a nonzero clip ID.
Opacity/blend and clipping therefore compose through the same existing
`graphics_state_id` on text, embedded text, glyph runs, images, and paths.

## Ownership and identity

Clip commands are validated with the generic PATH grammar, copied, and charged
to `max_resource_bytes`. Point components and affine components canonicalize
negative zero. Equal normalized geometry/rule/transform deduplicates to one clip
ID.

Registration failure publishes nothing.

## Backend lowering

The clip transform is applied numerically to each local displayed-space path
point:

```text
X = a*x + c*y + e
Y = b*x + d*y + f
pdfY = page_height - Y
```

The content stream then emits the transformed path followed by:

```text
W n
```

or:

```text
W* n
```

inside the same outer `q ... Q` operation scope introduced for graphics state.

No persistent `cm` operator is used for the clip transform; therefore the
operation being clipped keeps its own coordinate system and transform.

A graphics state with clip ID 0 retains the exact #100 `q /GS<n> gs ... Q`
content shape.

## Qualification

- invalid/empty path, invalid rule, NaN/singular transform;
- caller-command ownership and negative-zero deduplication;
- resource-budget transactional failure;
- V1 vs V2 clip=0 graphics-state dedup;
- invalid clip ID rejection;
- nonzero and even-odd clip operators;
- transformed clip coordinates;
- clip + opacity/blend composition;
- text/image/path consumers;
- scope isolation from adjacent operations;
- PDFium render assertions;
- repeated-finish determinism;
- Windows installed-package smoke.
