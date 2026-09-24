# Composer SVG V3A advanced primitive lowering design

Tracks #124 under #120 / #116.

## Scope

SVG V3A lowers semantics that now have exact Composer/PDF primitives:

- non-conformal and skewed stroked transforms through PATH V4 CTM;
- userSpaceOnUse pattern paint through Composer tiling-pattern resources;
- root/group opacity through isolated transparency Form XObjects;
- original SVG painting order across PATH, symbol/use Forms, and opacity-group
  Forms.

objectBoundingBox resource units and template inheritance remain V3B.

## PATH V4 lowering

SVG geometry remains in local user coordinates.

Each staged path stores the complete SVG-to-displayed-page matrix in PATH V4
`options.transform`.

Stroke width, dash lengths/phase, cap/join/miter, gradient coordinates, and
pattern coordinates remain local user-space operands. qpdf applies the native
PDF CTM inside the PATH q/Q scope.

This replaces the earlier conformal-only numeric geometry transform for normal
drawable paths and makes non-uniform scale/skew exact.

Clip resources remain outside the PATH q/cm scope and therefore continue to
receive the full referencing-element transform explicitly.

## User-space patterns

V3A accepts pattern definitions with:

- explicit `patternUnits="userSpaceOnUse"`;
- absent or `patternContentUnits="userSpaceOnUse"`;
- finite numeric x/y/width/height;
- width/height > 0;
- optional finite non-singular `patternTransform`;
- drawable/group tile content using existing bounded SVG primitives.

Pattern content is captured as owned tokens in the definition pre-pass.

Nested pattern paint dependencies and gradient dependencies are preflighted.
Pattern-to-pattern cycles are rejected.

V3A deliberately rejects symbol/use recursion inside pattern tiles.

At publish, a referenced pattern is materialized once into a Composer
tiling-pattern paint. The tile builder replays a synthetic SVG whose viewBox is
the pattern x/y/width/height and whose defs table is copied from the source SVG.

The tiling paint transform is:

```text
patternTransform × translate(x, y)
```

The referencing path's PATH V4 CTM then maps pattern and geometry together into
displayed page space.

## Root/group opacity

SVG opacity is not inherited. Non-1 opacity on an SVG root or g element is not
multiplied into child paint alpha.

Instead the parser consumes the complete subtree into a staged opacity group.

The synthetic group SVG:

- uses the destination displayed bounds as its root viewBox;
- copies the immutable defs token set;
- writes the group's resolved inherited fill/stroke presentation values;
- resets opacity to 1;
- applies the original complete SVG-to-displayed transform as a wrapper matrix;
- replays the captured child tokens unchanged.

Publish creates a Form with:

```text
TRANSPARENCY_GROUP | ISOLATED
```

and places that Form once under a Normal graphics state whose fill/stroke alpha
equals the SVG group opacity.

Nested opacity groups remain in the captured tokens and recurse through the
same draw_svg -> isolated Form lowering.

Root defs are skipped while capturing root opacity because the synthetic SVG
already injects the original defs table.

Group/root clip-path remains unsupported in V3A; no clip intersection
approximation is introduced.

## DOM painting order

Earlier SVG phases staged PATH and use/Form operations in separate vectors.

V3A assigns one monotonic order value while parsing to:

- PATH;
- use/Form;
- opacity-group Form.

Resource publication remains type-specific and transactional. After all
publication succeeds, the newly appended parent operation slice is sorted back
into this original SVG order.

Duplicate/missing order values are treated as backend errors.

This preserves normal SVG overpainting while retaining the existing resource
registration implementations.

## Transactional behavior

draw_svg captures operation, paint, clip, graphics-state, Form and resource-byte
snapshots before publication.

Rollback frees:

- copied PATH command/dash buffers;
- new gradient stop arrays;
- new tiling-pattern PDF snapshot bytes;
- new clip command arrays;
- new Form PDF snapshot bytes.

Then all counts and resource bytes are restored.

The synthetic opacity-group child Composer is synchronous and leaves no
retained callback/user pointers.

## Security and boundary

Unchanged fail-closed boundaries include:

- no DTD/entities/script;
- no filesystem/network/external URLs;
- no browser CSS cascade;
- no filters or masks;
- no objectBoundingBox units in V3A;
- no paint-template href inheritance in V3A;
- no symbol/use recursion inside pattern tile content;
- no group/root clip-path composition.

## Qualification

- non-uniform/skewed PATH with native stroke width and dash operands;
- direct PATH V4 CTM evidence;
- tiling-pattern fill/stroke and PatternType 1 resource inspection;
- pattern transform and repeated-cell PDFium rendering;
- pattern dependency cycle and unsupported-unit rejection;
- overlapping children under isolated group opacity;
- nested group opacity;
- root opacity;
- DOM order evidence with a later ordinary path over an opacity group;
- group Form /Group inspection;
- transaction rollback after failed Form/pattern resource publication;
- deterministic finish;
- installed-package V3A smoke;
- Linux/macOS/Windows full CI.
