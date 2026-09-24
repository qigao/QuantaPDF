# Composer SVG V4B local mask lowering design

Tracks #132 under #128.

## Goal

Lower bounded local SVG mask resources to the existing Composer Form-backed
soft-mask primitive without raster flattening or a second PDF backend.

## Public surface

No new public API is added by SVG V4B.

SVG lowering reuses:

- transparency-group Forms;
- `quantapdf_composer_add_soft_mask()`;
- graphics-state V3 `soft_mask_id`;
- compound clip resources;
- PATH V4 affine CTM.

ABI remains v2.

## Definition graph

Root `defs` may contain:

```xml
<mask id="..." ...> ... </mask>
```

A mask definition owns copied XML tokens plus:

- maskUnits;
- maskContentUnits;
- x/y/width/height region expressions;
- Alpha or Luminosity mode;
- local mask dependencies referenced by children.

Defaults:

```text
maskUnits        = objectBoundingBox
maskContentUnits = userSpaceOnUse
mask-type        = luminance
x                = -10%
y                = -10%
width            = 120%
height           = 120%
```

Only local fragment references are accepted.

A definition pre-pass collects mask tokens and dependency edges before any
Composer mutation. DFS rejects:

- cycles;
- unresolved mask IDs;
- wrong-kind mask references;
- external references.

## Presentation property

`mask` is a non-inherited presentation property.

Supported:

```text
mask="none"
mask="url(#localMask)"
style="mask:url(#localMask)"
```

Normal shapes/groups/root accept the ordinary presentation-property path.
`use` V1 accepts the direct `mask` attribute.

The outer mask reference is removed from a captured group/root synthetic child
SVG so the same mask is not recursively applied to itself.

## Coordinate model

### Leaf PATH

Leaf PATH already carries:

- exact local geometry bbox;
- full SVG-to-displayed-space PATH V4 transform.

For objectBoundingBox maskUnits/contentUnits, the exact staged local bbox is
used.

For bbox:

```text
B = translate(x0,y0) * scale(width,height)
```

### Region resolution

objectBoundingBox region numbers are fractions. Percentages divide by 100.

userSpaceOnUse accepts explicit numeric/px region values. Percentage
region values and omitted default region values depend on the active SVG
viewport percentage context; V4B does not substitute the target geometry bbox,
so those cases remain fail-closed.

A zero-area or unavailable bbox fails closed whenever required by
objectBoundingBox units or content conversion.

### Content coordinate conversion

The mask Form native width/height remain in maskUnits coordinates.

Synthetic child SVG viewBox uses maskContentUnits for the same region:

- same units: direct region;
- maskUnits=objectBoundingBox + contentUnits=userSpaceOnUse:
  map the region through B;
- maskUnits=userSpaceOnUse + contentUnits=objectBoundingBox:
  map through inverse B.

### Soft-mask placement

The source Form is placed with:

```text
targetTransform
  * maskUnitsMatrix
  * translate(region.x, region.y)
```

and registered through `quantapdf_composer_add_soft_mask()`.

## Compositing layers

### Leaf PATH

A masked leaf composes:

```text
fill/stroke alpha
blend mode
compound clip
soft mask
```

through one graphics-state V3 ID.

### Group/root

A group/root carrying opacity and/or a mask is captured as the existing
isolated transparency Form.

The mask is not pushed into its children. Instead the outer Form placement
receives the graphics-state V3 containing:

- group opacity;
- compound clip chain;
- soft mask.

This preserves SVG group compositing order.

### use

A masked `use` applies the soft mask to the Form placement.

V1 supports exact userSpaceOnUse masks at this outer layer. objectBoundingBox
or percentage region semantics requiring the symbol's exact painted geometry
bbox remain fail-closed rather than approximated from its viewport.

## Mask content

Mask content is converted to a synthetic SVG and processed recursively by the
same SVG lowerer.

Supported child content therefore includes the existing bounded subset:

- path/shape;
- groups;
- local paints;
- local clip resources;
- symbol/use;
- nested acyclic local masks.

No external I/O or raster flattening occurs.

## Mode

`mask-type: alpha` lowers to Composer Alpha soft masks.

`mask-type: luminance` / default lowers to Composer Luminosity soft masks.
The underlying Composer mask wrapper uses an isolated DeviceRGB transparency
group, so supported sRGB mask content has explicit luminosity semantics.

## Transactionality

The global SVG publish snapshot includes:

- operation count;
- paint count;
- clip count;
- graphics-state count;
- soft-mask count;
- form count;
- resource bytes.

Failed mask Form/soft-mask creation or later PATH/Form publication restores all
counts and frees newly owned Form/paint/clip payloads.

Leaf PATH publication also keeps a local pre-publication rollback snapshot
covering newly created mask Forms and soft-mask IDs.

## Security/fail-closed boundary

Rejected:

- external mask URLs;
- unresolved/wrong-kind/cyclic mask references;
- filter-based mask content;
- external images/resources;
- CSS cascade;
- unsupported XML active content;
- zero-area bbox when objectBoundingBox is needed;
- objectBoundingBox group/root/use masks when the exact outer target bbox is
  unavailable;
- raster approximation of unsupported masks.

## Qualification

- leaf Alpha objectBoundingBox mask;
- leaf Luminosity userSpace mask;
- maskUnits/maskContentUnits conversion;
- group mask + opacity + compound clip;
- root mask;
- mask content containing local use;
- nested mask dependency cycle rejection;
- unresolved/wrong-kind/external reference rejection;
- zero-area objectBBox rejection;
- transactional resource-limit rollback;
- direct /SMask dictionary evidence;
- deterministic repeated finish;
- PDFium rendering;
- installed-package V4B fixture;
- Linux/macOS/Windows full CI.
