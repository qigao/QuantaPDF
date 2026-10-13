# #269 — bounded source-width polyline stroke in fractional grid (v0)

This is a new, **explicit private C11 stroke-only raster entrypoint** stacked
on #277. It never changes the existing V1 stroke engine, the fill-only
fractional Path+Clip entrypoint, the original jsPDF negative gate or the
public PDFium SDK.

## Exact supported semantics

- One borrowed, resource-free selected page with a **single Stroke-only
  open PATH** of 1–24 non-degenerate MOVE/LINE segments; no CLOSE, cubic,
  dashes, Clip resources, graphics states or source-affine transforms.
- Opaque solid color, a finite source width in (0,16] PDF display points,
  Butt Cap, Miter Join with limit [1,10] and a PDF-style bevel fallback when
  the miter would exceed that limit.
- Use the existing validated double-precision page device grid: 72/96/144
  DPI, zero display origin, no intrinsic/requested rotation, UserUnit=1.
  The sample positions are the same 4×4 device-subpixel convention as
  NativeDeviceGrid. The point-to-device X/Y ratios may be unequal.
- At each device sample: test rectangular strips of every source-space
  line segment (flat end caps), then the outer join polygon for each pair
  of consecutive segments. A miter uses the bevel wedge plus the outer
  miter triangle. A miter exceeding the limit uses the bevel wedge only.
  Exact retraces and collinear continuation use the union of the strips.
- All segments and joins of **one source Stroke** are a Boolean union
  before blending once into an RGBA pixel: no double alpha from repeated
  edges/joins or retracing. Transparent outside the stroke.

## Bounded ownership

The source IR is borrowed; command/segment/join arrays are bounded stack
values; the only heap allocation is the uniquely owned output RGBA image.
Grid/IR precision, pixel, RGBA byte, edge and 16-sample-by-edge work
admissions are checked *before* allocation. The entire output structure is
reset on every error. No PDFium/QPDF fallback or copied source state.

The very narrow source subset is intentional. No arbitrary caps/joins,
dash, clip, multi-operation compositing, nonidentity transform or cubic
stroke is silently accepted. The proposed miter/bevel model is **not yet
an independent pixel-accurate PDFium oracle**.

## Gate

One standalone strict C11 test checks a 32.5x24.75 point source page into
33x25 device pixels, 4-point miter and bevel difference, Butt start cap,
fractional-edge alpha, a 1-point stroke, retraced edge union/byte-identical
output, and transactional rejection of malformed/stroke/precision/budget
cases. It links no PDFium, qpdf or SDK wrapper.

The exact original jsPDF remains 1 stroked PATH + 1 filled PATH + W Clip,
and the original input still fails native v1/fill-only opt-in with
UNSUPPORTED and zero output. This new stroke-only primitive has **not**
qualified a full original jsPDF image against PDFium or authorized an
original-PDF positive. Next gate combines the correctly ordered source Stroke
before W Clip fill and checks full original-PDFium vs detached-native pixels,
interior goldens, repeats and caps/joins before changing any negative gate.

Keep #269 open, stacked PRs Draft/DO NOT MERGE, original corpus/W* counters
unchanged, public SDK PDFium-backed and #208 cutover blocked.
