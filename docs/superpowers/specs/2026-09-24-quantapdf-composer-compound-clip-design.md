# Composer compound clip intersection design

Tracks #129 under #128.

## Goal

Allow one existing graphics-state `clip_id` to represent the exact intersection
of several immutable Composer clip resources.

No draw option record changes.

## Public API

```c
#define QUANTAPDF_COMPOSER_MAX_CLIP_COMPONENTS ((size_t)64u)

quantapdf_composer_add_clip_intersection(
    composer,
    clip_ids,
    clip_count,
    &out_clip_id);
```

V1 accepts 2..64 nonzero clip IDs that already exist in the same Composer.

## Canonical identity

Clip resources have two private kinds:

- PATH — the existing copied commands/rule/transform resource;
- INTERSECTION — a copied array of canonical leaf PATH clip IDs.

Registration recursively flattens any referenced intersection. The resulting
leaf IDs are sorted and duplicate IDs are removed.

Consequences:

```text
A ∩ B == B ∩ A
(A ∩ B) ∩ C == A ∩ (B ∩ C)
A ∩ A == A
```

If canonicalization produces one leaf, the API returns that existing clip ID
rather than publishing another resource.

Intersection resources therefore never reference another intersection, and
cycles are structurally impossible.

The flattened leaf count is bounded by
`QUANTAPDF_COMPOSER_MAX_CLIP_COMPONENTS`.

## Ownership and budget

The Composer copies only the canonical leaf-ID array.

```text
resource charge = leaf_count * sizeof(quantapdf_composer_clip_id)
```

Referenced PATH resources remain independently owned and immutable.

Equal canonical leaf arrays deduplicate.

## PDF lowering

Existing PATH clip bytes remain unchanged.

For an intersection resource the operation scope emits every referenced leaf:

```text
q
  <leaf A path> W/W* n
  <leaf B path> W/W* n
  <leaf C path> W/W* n
  /GS<n> gs
  <operation>
Q
```

PDF clipping paths are cumulative, so this is the exact intersection. No path
boolean operation or rasterization occurs.

Each leaf retains its own fill rule and displayed-space affine transform.

## Compatibility

Existing path clip IDs, graphics-state IDs, and V1/V2 draw option layouts do not
change.

A graphics state still contains exactly one clip ID; that ID can now refer to a
PATH or INTERSECTION resource.

## Qualification

- null/count/zero/out-of-range validation;
- duplicate-only collapse;
- reversed-order canonical deduplication;
- nested intersection flattening;
- mixed nonzero/evenodd leaf rules;
- transformed leaf clips;
- exact member-array resource budget;
- PATH and Form consumers;
- outer q/Q scope isolation;
- direct cumulative W/W* evidence;
- PDFium intersection rendering;
- deterministic repeated finish;
- installed-package consumer;
- Linux/macOS/Windows full CI.
