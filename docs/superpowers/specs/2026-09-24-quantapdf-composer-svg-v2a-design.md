# Composer SVG V2A direct lowering design

Tracks #109 under #104.

## Scope

V2A extends the existing streaming SVG parser only with semantics that do not
require a cross-element reference graph:

- numeric `stroke-dasharray` / `stroke-dashoffset`;
- inherited `fill-opacity` / `stroke-opacity`;
- leaf `opacity`;
- A/a elliptical path arcs;
- root `preserveAspectRatio`.

Reference-bearing gradients, clipPath, reusable definitions, and local use
resolution remain in SVG V2B.

## Opacity

`fill-opacity`, `stroke-opacity`, dash array, and dash offset remain inherited
presentation properties in the existing `paint_style`.

`opacity` is explicitly reset to 1 during child style derivation because SVG
opacity is not inherited. On drawable leaf shapes:

```text
effective fill alpha   = opacity * fill-opacity
effective stroke alpha = opacity * stroke-opacity
```

The staged path stores those two alpha values. Publish registers/deduplicates a
Normal Composer graphics state only when either effective alpha differs from 1.

Non-1 opacity on root or group containers is rejected in V2A. Correct group
opacity requires whole-group compositing and is not equivalent to per-child
alpha multiplication.

## Dash lowering

V2A accepts numeric/user-unit dash values and px scalar dash offsets.

- `none` or an all-zero array lowers to solid;
- negative dash lengths are malformed;
- odd arrays are duplicated to an even list;
- the final array is bounded by `QUANTAPDF_COMPOSER_MAX_DASH_COUNT`;
- dash lengths and offset are multiplied by the same conformal scale used for
  stroke width;
- negative offsets normalize modulo the full repeated pattern length into the
  nonnegative PDF dash phase.

Non-conformal transforms on stroked paths remain unsupported because the
generic Composer stroke-width/dash contract is scalar.

## Elliptical arcs

A/a endpoint arcs are normalized during path parsing.

- negative radii are treated as absolute values;
- zero radius lowers to a line segment;
- equal start/end points emit no segment;
- large-arc and sweep flags must be exactly 0 or 1;
- radii are corrected when the endpoint ellipse has no solution;
- each arc is split into at most 90-degree pieces;
- every piece lowers to a cubic Bézier.

No arc primitive reaches Composer or qpdf.

## preserveAspectRatio

Default is `xMidYMid meet`.

Supported V2A values:

- `none`;
- all nine xMin/xMid/xMax + YMin/YMid/YMax alignments;
- optional `meet` or `slice`.

`meet`/alignment affect only the root viewport matrix. `none` permits
non-uniform fill-only geometry; stroked geometry still follows the existing
conformal-stroke restriction.

`slice` additionally stages a destination-bounds viewport clip.

## Transactional publish

Parsing creates no Composer resources.

At publish:

1. preflight command + dash bytes and reserve operation capacity;
2. snapshot clip count, graphics-state count, and resource bytes;
3. if slice is active, register/deduplicate a bounds clip;
4. register/deduplicate per-path opacity+clip graphics states;
5. allocate copied command/dash payloads;
6. publish all PATH operations and resource bytes together.

Any failure before step 6 frees newly registered clip geometry and restores the
resource/state snapshots. Existing resources and operations remain unchanged.

## Security boundary

V2A does not alter the XML security model:

- no DTD/entity expansion;
- no script;
- no external image/reference access;
- no browser CSS cascade;
- no filesystem/network I/O.

Group opacity remains fail-closed. Local references remain fail-closed until
V2B.

## Qualification

- inherited odd dash list + negative offset;
- fill/stroke opacity × leaf opacity;
- A/a arc conversion and malformed flags;
- default meet, none, and slice viewport behavior;
- slice clip content evidence and PDFium render bounds;
- group opacity rejection;
- existing non-conformal stroke rejection;
- atomic max-operation/resource failure;
- deterministic repeated finish;
- installed-package SVG V2A smoke;
- full Linux/macOS/Windows CI.
