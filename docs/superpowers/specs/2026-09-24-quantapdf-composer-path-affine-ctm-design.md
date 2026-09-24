# Composer PATH V4 affine CTM design

Tracks #119 under #116.

## Goal

Support arbitrary finite non-singular affine placement for generic PATH while
preserving native PDF stroke width, dash, cap, join, miter and paint semantics
under non-conformal transforms.

The transform belongs to the PATH operation, not to SVG or any backend-specific
layer.

## Public ABI

Append a V4 tail to `quantapdf_composer_path_options`:

```c
quantapdf_affine_transform transform;
```

V1, V2 and V3 SIZE macros remain frozen at their historical ABI boundaries.

V4 semantics:

- all-zero transform record means identity;
- explicit identity is also canonical identity;
- any other transform must be finite and non-singular.

The owned operation stores a normalized V4 option record. Legacy callers are
normalized to identity during publication, so the backend sees one shape.

## PDF coordinate conversion

Public PATH coordinates are displayed-page-space (top-left origin, y down).

For page height H and public transform:

```text
X = a*x + c*y + e
Y = b*x + d*y + f
```

the PDF CTM applied to already-converted y-up PATH coordinates is the conjugate:

```text
[a, -b, -c, d,
 c*H + e,
 H*(1-d) - f]
```

This matrix is emitted only for non-identity transforms.

## Lowering order

The existing PATH stream remains inside `q ... Q`.

For non-identity V4:

```text
q
  <affine cm>
  <stroke/fill color or Pattern>
  <width/cap/join/miter>
  <dash>
  <path geometry>
  <paint operator>
Q
```

The native PDF CTM therefore transforms:

- geometry;
- stroke width;
- dash spacing/phase;
- caps and joins;
- gradient/pattern paint behavior;
- fill and stroke together.

No numerical pre-scaling of stroke/dash occurs.

## Compatibility

V1–V3 and V4(identity) must remain byte-identical to existing PATH output.

`draw_path_dashed()` uses the same V4 path-options tail, so no parallel
transformed-dash API is added.

Paint IDs, graphics-state IDs and dash ownership are unchanged.

## Qualification

- V3 vs V4(identity) byte identity;
- all-zero identity;
- NaN/singular transform rejection;
- direct conjugated `cm` evidence;
- non-uniform scale with fill;
- transformed dashed stroke retaining original width/dash operands;
- gradient fill under CTM;
- PDFium render assertions;
- installed-package smoke after version integration;
- full Linux/macOS/Windows CI.
