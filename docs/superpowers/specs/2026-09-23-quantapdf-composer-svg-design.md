# QuantaPDF Composer SVG Ingestion Design

**Date:** 2026-09-23  
**Tracking:** #83  
**Feature release:** 2.13.0

## Goal

Accept a deliberately constrained static SVG document and lower every visible
shape into the generic Composer path IR introduced by #69.

SVG is an ingestion layer, not a second drawing backend:

```
caller SVG bytes
      ↓
bounded parser / normalization
      ↓
Composer PATH operations
      ↓
private qpdf backend
      ↓
PDF
```

The SVG parser never emits PDF syntax.

## Public API

`quantapdf_composer_draw_svg()` accepts:

- an existing Composer;
- destination page index;
- caller-owned SVG bytes plus explicit byte count;
- destination rectangle in normal displayed page space;
- a size-tagged SVG options record reserved for additive extensions.

The input buffer is parsed synchronously and is never retained.

## V1 document model

V1 supports one root `svg` element with a required `viewBox`, plus nested
`g` elements.

Supported graphics elements:

- `path`;
- `rect`;
- `line`;
- `polyline`;
- `polygon`;
- `circle`;
- `ellipse`.

Nested `svg` is not part of V1.

The root viewBox is stretched into the caller-provided destination rectangle.
`preserveAspectRatio` is intentionally not accepted in V1; callers that need
aspect-preserving placement can choose a matching destination rectangle.

## Path grammar

Supported commands are:

- M/m;
- L/l;
- H/h;
- V/v;
- C/c;
- S/s;
- Q/q;
- T/t;
- Z/z.

Quadratic Q/T segments are converted exactly into cubic Bézier segments before
the path enters Composer state.

Ellipses and circles are represented as four cubic Bézier segments using the
standard kappa approximation.

Elliptical-arc A/a commands are unsupported in V1 rather than approximated.

## Style

The inherited V1 paint model supports:

- `fill`;
- `stroke`;
- `fill-rule`;
- `stroke-width`;
- `stroke-linecap`;
- `stroke-linejoin`;
- `stroke-miterlimit`;
- the same properties in a bounded inline `style` attribute.

Colors support opaque `#rgb`, `#rrggbb`, and a small deterministic set of
basic named colors. `none` disables fill or stroke.

Opacity, alpha colors, gradients, patterns, CSS selectors/cascade, currentColor,
and paint servers are outside V1.

## Transforms

V1 accepts nested SVG transform lists:

- matrix;
- translate;
- scale;
- rotate, including an optional center;
- skewX;
- skewY.

Transform items are concatenated in SVG list order. The full element CTM is
applied to every path point before publication.

The existing Composer path IR stores scalar stroke width rather than a graphics
state matrix. Therefore:

- fill-only geometry accepts any finite affine transform;
- stroked geometry requires a conformal transform: translation,
  rotation/reflection, and uniform scale;
- non-uniform scale or skew on a stroked path fails
  `QUANTAPDF_ERROR_UNSUPPORTED`.

This avoids silently approximating SVG stroke semantics.

## Security boundary

The parser is intentionally not a general XML/browser engine.

It performs no filesystem, URL, DNS, image, stylesheet, or font access.

The following are rejected before any Composer operation is published:

- DOCTYPE and entity constructs;
- CDATA;
- scripts;
- `use`;
- `image`;
- `text`;
- external references;
- unknown elements/attributes;
- unsupported presentation/CSS properties.

Attribute entity references are not expanded. Any `&` in an attribute value
is rejected in V1.

Non-whitespace text nodes are unsupported.

## Bounds

The raw SVG input is capped at 16 MiB.

Element nesting is capped at 64 levels.

Output path count is bounded by the Composer's remaining operation capacity.
Path command storage is charged to `max_resource_bytes`.

All numeric parsing requires finite values. Coordinates that cannot fit the
public float path representation fail unsupported.

## Transactionality

SVG ingestion is all-or-nothing.

The implementation:

1. parses the complete input;
2. normalizes all geometry/styles/transforms into temporary path vectors;
3. checks total operation and command-byte budgets;
4. reserves the complete operation batch;
5. allocates copies for every path;
6. publishes the entire batch only after all allocations succeed.

A parse, validation, capacity, or allocation failure publishes no partial SVG.

The operation capacity itself may grow during a failed allocation attempt, but
no observable draw operation or resource-byte accounting is committed.

## Determinism

SVG traversal follows document order.

No hash iteration, external resource lookup, locale-dependent number
formatting, or time/random input is used.

Identical Composer state remains byte-identical across repeated `finish()`
calls.

## Verification

Focused tests cover:

- viewBox to destination mapping;
- rectangle/circle/ellipse/polygon/polyline;
- Q/T and S/C path lowering;
- style inheritance and inline style;
- nested translate/scale transforms;
- PDF content evidence and PDFium rendering;
- DOCTYPE/entity/script/image/use/text rejection;
- arc/opacity rejection;
- non-conformal stroked-transform rejection;
- malformed viewBox/path/points/XML;
- duplicate attributes;
- operation-capacity failure with no partial publication;
- repeated-finish byte equality;
- Linux static plus ASan/UBSan;
- Windows/macOS full CI;
- Windows installed-package consumer invocation of `draw_svg()`.

## Deferred

Future additive work may cover:

- A/a arcs;
- rounded rectangles;
- preserveAspectRatio;
- richer colors;
- gradients/patterns;
- exact non-conformal stroke transforms;
- safe reusable definitions;
- a larger CSS subset.

Those capabilities must continue to lower into stable Composer primitives
rather than expose an SVG/PDF backend.
