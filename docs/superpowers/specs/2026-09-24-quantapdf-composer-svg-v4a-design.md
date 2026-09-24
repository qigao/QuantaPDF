# Composer SVG V4A compound clip lowering

Tracks #131 under #128.

## Goal

Remove the remaining SVG clip-intersection approximations/rejections by lowering
every active clipping component to immutable Composer clip resources and
combining them through the native compound-clip primitive.

No path boolean geometry, rasterization, or SVG-specific PDF backend is added.

## Staging model

The SVG parser carries inherited clip components separately from paint/style.

A staged drawable records its own local geometry bbox, the referencing transform
for each active clip component, inherited root/group clip components, and an
optional local clip-path component.

A staged symbol/use records inherited clip components plus an optional local
clip-path. A staged opacity group records the outer active clip chain while the
captured child SVG renders its internal/local clips normally.

clip-path remains a non-inherited SVG presentation property. Group/root
propagation is explicit through the parser context active-clip list.

## Publication

Each clip component is first materialized independently as an ordinary Composer
clip ID, retaining its own fill rule, transform, and objectBoundingBox mapping
when a leaf drawable provides a valid local bbox.

Extra viewport clips are materialized independently for root
preserveAspectRatio=slice bounds and symbol/use slice viewports.

All nonzero IDs for one operation are passed through
quantapdf_composer_add_clip_intersection().

The compound resource canonicalizes leaf IDs. The PDF backend emits each leaf
path cumulatively inside one q/Q scope, giving the exact intersection.

## Supported consumers

PATH combines root/group inherited clips, local leaf clip, and optional root
slice viewport clip.

symbol/use Form placement combines inherited clips, local use clip, optional
root slice clip, and optional symbol slice viewport clip.

Opacity-group Form placement keeps subtree-local clips inside the captured child
SVG while ancestor/group clips and root slice clip remain on the outer Form.

## Nested clipPath references

V4A supports local clipPath-to-clipPath chains when every link uses
userSpaceOnUse. Materialization recursively registers each path clip and
intersects the IDs.

Definition preflight rejects cycles, unresolved/wrong-kind/external references,
and nested-reference links involving objectBoundingBox units.

## objectBoundingBox

Leaf drawable objectBoundingBox clip-path continues to use the exact staged
local geometry bbox from SVG V3B and can participate in a compound intersection
with user-space ancestor clips.

Root/group and use-level explicit objectBoundingBox clip references remain
fail-closed until an exact container/reference bbox contract is modeled.

## Transactional behavior

The existing SVG publish snapshot owns operations, paints, clip resources
(including intersection member arrays), graphics states, Forms, and resource
bytes. Any V4A failure restores all snapshot counters and frees new clip
commands/member arrays.

## Qualification

- four-way PATH intersection: viewport slice + root + group + bbox leaf;
- group opacity under root/group clips;
- symbol slice viewport + inherited/local clip intersection;
- local nested userSpace clipPath chain;
- cycle and nested bbox-chain rejection;
- cumulative W/W* evidence and PDFium rendering;
- resource-budget rollback and deterministic finish;
- installed-package V4A fixture;
- Linux/macOS/Windows full CI.
