# Composer SVG V3B objectBoundingBox and paint-template design

Tracks #125 under #120 / #116.

## Scope

V3B completes exact geometry-dependent local paint semantics:

- gradientUnits=objectBoundingBox (including the SVG default);
- clipPathUnits=objectBoundingBox;
- patternUnits=objectBoundingBox (including the SVG default);
- patternContentUnits=objectBoundingBox;
- local same-kind gradient href/xlink:href templates;
- local pattern href/xlink:href templates.

No new PDF backend path or public ABI is introduced.

## Exact staged geometry bbox

Every staged PATH retains its local path commands and PATH V4 CTM.

Before resource registration, QuantaPDF computes an exact local geometry bbox
from the path commands:

- line and close segments include their endpoints;
- cubic Bézier extrema are found analytically from derivative roots in (0,1);
- isolated moveto commands do not contribute;
- stroke width/dash/cap/join are not included.

This is the SVG geometry/fill bbox used by objectBoundingBox resources.

If either bbox dimension is zero, any objectBoundingBox paint or clip reference
fails closed.

## objectBoundingBox coordinate system

For local bbox [x0,y0,x1,y1]:

```text
B = translate(x0,y0) * scale(x1-x0, y1-y0)
```

The staged PATH commands remain local and PATH V4 still applies the element CTM
exactly once.

### Gradient

```text
paint transform =
    elementCTM * unitsMatrix * gradientTransform
```

where unitsMatrix is identity for userSpaceOnUse and B for objectBoundingBox.

Gradient coordinates stay in their gradient coordinate system.

### clipPath

clipPath and child transforms are normalized into clip commands during the
definition pass.

At publication:

```text
clip transform =
    elementCTM * unitsMatrix
```

### Tiling pattern

Pattern Form native coordinates remain in patternUnits coordinates.

```text
pattern matrix =
    elementCTM
  * unitsMatrix
  * patternTransform
  * translate(x,y)
```

Pattern width/height and XStep/YStep remain in patternUnits coordinates.

The synthetic tile SVG viewBox is expressed in patternContentUnits for the
same tile rectangle:

- same units: x/y/width/height directly;
- patternUnits=objectBoundingBox + contentUnits=userSpaceOnUse:
  map the tile rectangle through B;
- patternUnits=userSpaceOnUse + contentUnits=objectBoundingBox:
  map the user-space tile rectangle through inverse B.

This provides all four exact patternUnits/patternContentUnits combinations
without a second backend representation.

## Length parsing

objectBoundingBox resource coordinates accept unitless numbers or percentages.
Percentages divide by 100.

userSpaceOnUse retains the bounded numeric/px contract. Percentages in
userSpaceOnUse remain unsupported because viewport-relative SVG percentage
semantics are not yet modeled by Composer resources.

Gradient defaults under objectBoundingBox follow SVG:

Linear:
- x1=0, y1=0, x2=1, y2=0.

Radial:
- cx=0.5, cy=0.5, r=0.5;
- fx/fy default to cx/cy;
- fr=0.

Pattern defaults:
- patternUnits=objectBoundingBox;
- patternContentUnits=userSpaceOnUse;
- x/y=0;
- width/height=0 (therefore invalid until inherited/overridden to positive
  values).

clipPathUnits continues to default to userSpaceOnUse.

## Local template normalization

Definitions are first parsed into immutable raw records with attribute-presence
metadata and local fragment href.

A DFS normalization pass runs before render/publish.

### Gradient templates

Only local gradients of the same kind may be referenced.

Inherited when omitted:
- gradientUnits;
- gradientTransform;
- type-specific coordinates.

Stops:
- any local stop list overrides inherited stops;
- an empty local stop list inherits the template stop list.

After inheritance, defaults and units-specific coordinate parsing are applied,
then the stop list is finalized to Composer's 0..1 endpoint contract.

### Pattern templates

Only local patterns may be referenced.

Inherited when omitted:
- patternUnits;
- patternContentUnits;
- patternTransform;
- x/y/width/height.

Content:
- non-empty local child tokens override inherited content;
- empty local content inherits template tokens and their paint dependencies.

Template cycles, unresolved IDs, and wrong-kind references fail closed.

## Transactional publication

Definition parsing/normalization and bbox computation have no Composer side
effects.

Existing V3A publication snapshots cover paint/clip/state resources and owned
tiling-pattern snapshot bytes. objectBoundingBox registration uses the same
rollback path.

## Security boundary

Still rejected:

- external/file/network href;
- DTD/entities/script;
- browser CSS cascade;
- filters/masks;
- userSpace percentage paint coordinates;
- wrong-kind/unresolved/cyclic templates;
- zero-area object bbox.

## Qualification

- objectBBox gradient with exact Pattern matrix;
- objectBBox clipPath with exact W/n geometry;
- cubic extrema bbox (not control-point bbox);
- objectBBox pattern/content units;
- mixed bbox/userSpace pattern-content combinations;
- gradient template coordinate/stop inheritance;
- pattern template geometry/content inheritance;
- cycle/unresolved/wrong-kind template rejection;
- zero-area bbox rejection with transactional no-side-effect proof;
- PDFium rendering;
- deterministic repeated finish;
- installed-package V3B fixture;
- full Linux/macOS/Windows CI.
