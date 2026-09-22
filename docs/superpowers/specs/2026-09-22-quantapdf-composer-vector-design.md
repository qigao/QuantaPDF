# QuantaPDF Composer Vector Path Design

**Date:** 2026-09-22  
**Status:** Implemented by the #69 development branch

## Goal

Extend the existing Composer command IR with one reusable vector-path primitive
instead of adding a separate public function for every shape.

The public and private flow remains:

```
stable C ABI
   ↓
Composer-owned command IR
   ↓
private qpdf content-stream backend
   ↓
PDF
```

PDFGen is used as a feature/API reference only. QuantaPDF does not vendor its
serializer and does not add a second PDF writer.

## V1 path model

A path is an ordered fixed-layout array of commands:

- move-to
- line-to
- cubic Bézier
- close-path

Rectangles, polygons, circles/ellipses, and future higher-level vector features
are lowered to these commands. V1 therefore needs only
`quantapdf_composer_draw_path()` as a new exported drawing function.

The path-command record is intentionally fixed-layout because the API traverses
it as a C array. Future incompatible command payloads require a new type/API or
an explicit-stride API rather than silently changing array element size.

## Painting

The versioned path-options record supports:

- stroke on/off
- fill on/off
- opaque ARGB stroke/fill colors
- stroke width, including PDF hairline width 0
- nonzero/even-odd fill
- butt/round/square caps
- miter/round/bevel joins
- miter limit

Dash patterns and transparency are additive future work. V1 rejects translucent
stroke/fill instead of silently flattening it.

## Geometry

Coordinates follow the existing Composer page contract:

- points
- top-left origin
- x right
- y down

The qpdf backend alone performs the y-axis conversion to PDF content-stream
coordinates. No raw PDF operators are accepted through the public API.

## Ownership and failure

The Composer copies the command array before publishing the operation. The copy
is charged to the existing `max_resource_bytes` budget. Invalid sequences,
non-finite coordinates, invalid options, capacity overflow, and allocation
failure publish no partial operation.

A valid path contains a move command before drawing commands and at least one
line or cubic segment. Finish remains non-consuming and deterministic.

## Verification

Focused coverage validates:

- public argument and option boundaries
- path-sequence validation
- resource-budget failure
- line/close and cubic content emission
- top-left to PDF coordinate conversion
- fill/stroke rendering through PDFium
- repeated-finish byte equality
- ABI export baseline and package version bump
