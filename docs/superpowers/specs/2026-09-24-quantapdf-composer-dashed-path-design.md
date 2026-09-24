# Composer dashed path design

Tracks #93.

## Public contract

Keep the existing paint/stroke/fill record unchanged:

```c
quantapdf_composer_path_options
```

Add one independent dash input:

```c
#define QUANTAPDF_COMPOSER_MAX_DASH_COUNT ((size_t)64u)

typedef struct quantapdf_composer_dash_pattern {
    size_t struct_size;
    const float *lengths;
    size_t length_count;
    float phase;
} quantapdf_composer_dash_pattern;
```

and an additive draw entry point:

```c
quantapdf_composer_draw_path_dashed(
    composer,
    page_index,
    commands,
    command_count,
    path_options,
    dash_pattern);
```

This avoids copying all path paint fields into a second public style record and
does not append a borrowed pointer to the existing V1 path-options ABI.

## Validation

A dashed call requires a stroked path and:

- `1 <= length_count <= 64`;
- every length is finite and nonnegative;
- at least one length is strictly positive;
- zero-length elements are accepted and retain their PDF dash-array meaning;
- phase is finite and nonnegative.

A NULL pattern or a fill-only path is invalid for the dashed entry point.

## Ownership and atomicity

The Composer copies both path commands and dash lengths before publishing the
operation. No caller dash pointer is retained.

```text
resource charge =
    command_count * sizeof(path_command)
  + dash_count    * sizeof(float)
```

Overflow, resource-limit, allocation, or operation-capacity failure occurs
before publication. Temporary command/dash copies are released on failure.

The existing `quantapdf_composer_draw_path()` lowers through the same private
implementation with no dash array, preserving the existing solid-path byte
shape.

## Backend lowering

The existing PATH operation gains private owned dash state:

```text
PATH
  commands
  path_options
  dash_lengths[]
  dash_count
  dash_phase
```

For a stroked dashed path the qpdf backend emits:

```text
[length0 length1 ...] phase d
```

after line width/cap/join/miter setup and before path geometry. Solid paths emit
no `d` operator.

SVG `stroke-dasharray` / `stroke-dashoffset` can later lower through this
same path API/IR without a second graphics backend.

## Qualification

- simple two-element pattern;
- multi-element pattern and nonzero phase;
- zero-length element acceptance;
- NaN/negative/all-zero/count-bound rejection;
- fill-only rejection;
- caller-array copy ownership;
- resource-limit and max-operation transactional failure;
- exact PDF `d` operator evidence;
- PDFium rendering with visible on/off segments;
- solid path remains solid;
- repeated-finish determinism;
- installed-package compile/run smoke.
