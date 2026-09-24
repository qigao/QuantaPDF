# Composer gradient paint design

Tracks #101 under #99.

## Goal

Add reusable linear and radial gradient paint as Composer resources without
introducing a second vector backend or embedding borrowed stop arrays in path
operations.

## Public paint identity

quantapdf_composer_paint_id is a size_t-aligned reusable resource ID.
Paint ID 0 means the existing solid ARGB paint. Nonzero IDs currently refer
to registered gradient resources; the generic identity leaves room for later
pattern paint without another PATH ABI extension.

## Stops

V1 supports 2..64 quantapdf_composer_gradient_stop records.
Rules:
- first offset exactly 0;
- last offset exactly 1;
- offsets finite and strictly increasing;
- colors are opaque sRGB (alpha 0xff);
- caller stop storage is copied during registration.

Opacity belongs to the Phase 4 graphics-state primitive (#100), not gradient
stop colors.

## Geometry

Linear start/end points must be finite and distinct.
Radial radii are finite and nonnegative; the two circles may not be identical.
A zero start radius is valid.

Gradient geometry lives in local displayed-page coordinates. An all-zero
transform record means identity, allowing zero-initialized options. Any
nonzero transform must be finite and non-singular.

V1 spread behavior is PAD only, lowered exactly with PDF Extend true/true.
Repeat and reflect are not approximated.

## Path V3 attachment

Path options append fill_paint_id and stroke_paint_id.
Both IDs use size_t alignment, so they begin after the legacy V2 rounded
sizeof boundary. V1 remains the pre-2.18 layout; V2 remains the 2.18
graphics-state layout; V3 adds paint IDs.

Paint ID 0 uses existing fill_argb/stroke_argb. A nonzero fill paint requires
fill enabled; a nonzero stroke paint requires stroke enabled.

## Ownership and accounting

The private Composer paint record owns the copied stop array. Stop-array bytes
are charged to max_resource_bytes. Identical normalized gradients deduplicate
to the same paint ID. Failures publish no resource and release temporary
storage.

## PDF lowering

Linear gradients lower to ShadingType 2 and radial gradients to ShadingType 3.
Two-stop gradients use FunctionType 2. Multi-stop gradients use FunctionType 3
stitching of Type 2 segments. Color space is DeviceRGB.

Each page that references a paint creates a lightweight PatternType 2 wrapper
around the shared shading. Its matrix maps local displayed-space coordinates
to PDF y-up coordinates as:

    [a -b c -d e page_height-f]

PATH lowering selects Pattern color space for fill or stroke:

    /Pattern cs /P<n> scn
    /Pattern CS /P<n> SCN

No Pattern resource is emitted for paint ID 0, preserving solid output.

## Qualification

- V2 path vs V3 paint=0 byte identity;
- stop count/order/endpoints/opacity/NaN validation;
- linear/radial geometry validation and singular-transform rejection;
- stop ownership, resource accounting and deduplication;
- FunctionType 2 and 3 inspection;
- ShadingType 2 and 3 inspection;
- transformed gradient Pattern matrix;
- gradient fill and gradient stroke;
- PDFium color assertions;
- repeated finish determinism;
- Windows installed-package smoke.
