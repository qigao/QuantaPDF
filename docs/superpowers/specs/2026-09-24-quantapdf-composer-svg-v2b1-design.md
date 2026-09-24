# Composer SVG V2B1 local definition graph design

Tracks #112 under #110 / #104.

## Scope

SVG V2B1 adds a bounded side-effect-free local definition graph for:

- root `defs`;
- `linearGradient`;
- `radialGradient`;
- `stop`;
- `clipPath`;
- fragment-local `fill="url(#id)"`;
- fragment-local `stroke="url(#id)"`;
- fragment-local `clip-path="url(#id)"`.

Reusable symbol/use definitions are reserved for SVG V2B2.

## Two-pass parser

The SVG bytes are scanned twice.

### Definition pre-pass

The first pass:

1. validates XML structure;
2. registers every explicit SVG ID into one uniqueness set;
3. collects supported root-`defs` resources;
4. performs no Composer calls.

Definitions outside root `defs` are not resource definitions in B1.

### Render pass

The second pass:

- skips `defs` subtrees completely;
- keeps the existing style/transform inheritance pipeline;
- resolves forward/backward fragment references from the immutable definition
  table;
- stages only normal Composer PATH operations.

Missing, wrong-kind, or external references fail closed.

## Gradient boundary

V1 paint-server support is deliberately exact and narrow:

- `gradientUnits="userSpaceOnUse"` is required;
- default/objectBoundingBox units are rejected;
- spreadMethod is absent or `pad`;
- gradient template href/xlink:href is rejected;
- coordinates are finite numeric/px user units, not percentages;
- gradientTransform uses the existing SVG transform parser;
- 1..64 source stops are accepted and normalized to 2..64 Composer stops;
- stop offsets accept number or percentage in [0,1];
- source offsets are strictly increasing;
- missing 0/1 endpoint stops are synthesized from first/last colors;
- stop-color uses the existing bounded color parser;
- stop-opacity must resolve to 1 because Composer gradient V1 stops are opaque.

Linear definitions provide x1/y1/x2/y2.

Radial definitions provide cx/cy/r, optional fx/fy (default center), and
optional fr (default 0). fr > r is rejected rather than approximated.

## Gradient coordinate composition

Each staged path stores its complete SVG-to-displayed-page transform.

At publish, a referenced gradient uses:

```text
paint transform =
    referencing element user transform
  × gradientTransform
```

The gradient coordinates themselves remain in SVG user units. Therefore one
definition may be reused by differently transformed shapes and can deduplicate
when their normalized Composer paint is identical.

## clipPath boundary

V1 clip definitions require absent or `userSpaceOnUse` clipPathUnits.

A clipPath may contain supported shape/path children and nested groups. Its own
transform and child/group transforms are pre-applied to the stored local path
commands.

Only one effective clip rule may apply to the compound clip resource; mixed
nonzero/evenodd children are rejected.

Nested clip-path references inside a clip definition are rejected in B1.

At publish, the referencing path's SVG-to-displayed transform becomes the
Composer clip transform.

Root/group clip-path consumers are not implemented in B1; clip-path applies to
drawable leaf shapes only.

## Slice interaction

A V2A preserveAspectRatio=slice viewport clip and an explicit clip-path on the
same SVG draw are rejected in B1 because the current Composer graphics state
contains one clip resource. No geometric intersection approximation is used.

## Transactional publish

Parsing and definition collection are side-effect free.

Before publication, PATH operation capacity and copied command/dash bytes are
preflighted.

The publisher snapshots:

- paint count;
- clip count;
- graphics-state count;
- resource bytes.

It then registers/deduplicates referenced paints and clips, composes
opacity+clip graphics states, allocates PATH payloads, and publishes all
operations together.

Any failure before final publication frees newly allocated paint stop arrays and
clip commands and restores all snapshots.

## Security boundary

B1 never performs I/O.

Rejected:

- DTD/entities;
- scripts;
- external/file/network URLs;
- unresolved fragment IDs;
- wrong-kind references;
- duplicate IDs;
- objectBoundingBox gradient/clip units;
- gradient href templates;
- browser CSS cascade;
- unsupported definition kinds.

## Qualification

- forward definition references;
- linear/radial gradient resources;
- gradientTransform;
- gradient fill and stroke;
- transformed user-space clipPath;
- direct Pattern/Shading/Function and clip-content evidence;
- duplicate/unresolved/wrong-kind/external reference rejection;
- objectBoundingBox/stop-opacity/hard-stop rejection;
- transactional resource rollback under budget failure;
- deterministic repeated finish;
- PDFium rendering;
- installed-package smoke;
- Linux/macOS/Windows full CI.
