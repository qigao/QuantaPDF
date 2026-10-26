# Native original-source scoped mixed V6 — private opt-in profile

Tracks [#427](https://github.com/qigao/QuantaPDF/issues/427),
[#145](https://github.com/qigao/QuantaPDF/issues/145),
[#154](https://github.com/qigao/QuantaPDF/issues/154),
[#214](https://github.com/qigao/QuantaPDF/issues/214)
and complete [#208](https://github.com/qigao/QuantaPDF/issues/208).

## What changed

Previously the strict original-source V5 interface required exactly four
paints: one Path and one Image XObject while a frozen `W/W*` Clip was live,
followed by a **real `Q`** unclip and one of each afterwards. The existing
generic backend-free C11 V5 renderer already admits 2–4 source-ordered
Path/Image paints (at least one of each) with one immutable Clip/GS resource.

New private **Source V6** exposes that already-qualified raster admission
from unchanged *original* PDF Content Streams without inventing any
new opcodes or widening older source interfaces:

- Exactly **one `W` or `W*` Clip PATH**, terminated by `n`, with
  definition-time CTM frozen.
- **2–4 actual paints**, 1–2 opaque solid Path fills and 1–2 named RAW
  Gray8/RGB24 Image XObject `Do` operators, preserving source order.
- At least one operation while Clip is active and **at least one later
  operation after a genuine `Q` restores the unclipped graphics state**.
  Per-paint GS=1/Clip=1 or GS=0/no Clip is explicitly copied to detached
  owned IR. A second Clip, Clip reentry and scope-free first paint refuse.
- Inherited/local Image references, all Contents stream operators, q/Q,
  permitted positive axis-aligned CTMs, bounded bytes, page dimensions,
  content streams, four layers, independent decoded image source pixels
  and painted pixel work are checked without a provider fallback.
- Image or Path count imbalance, unsupported Stroke/Text/Form/shading/
  mask/color, malformed or incomplete scopes, missing resources, invalid
  transforms and exhausted budgets fail **atomically**, with zero output.

The older `quantapdf_qpdf_source_scoped_mixed_v5_to_ir()` retains its
**exact 2+2 paints, 2 clipped + 2 restored** signature. New
`quantapdf_qpdf_source_scoped_mixed_v6_to_ir()` is an explicit private
alternative. Both use **the existing**, unchanged
`quantapdf_ir_raster_scoped_mixed_v5()` pure-C11 renderer and its existing
caps; no new fallback or public ABI has been introduced.

## Original-PDF qualification

The test-only qpdf writer authors original source PDFs, and an independent
test-only PDFium instance renders **those unchanged original bytes**. The
new `quantapdf.native_source_scoped_mixed_v6` CTest qualifies:

- Clipped Path then restored Image, and clipped Image then restored Path
  (two paints), with exact outside-Clip probes.
- Two clipped paints then one restored; or one clipped then two restored
  paints, with each supported 1+2 and 2+1 Path/Image distribution.
- The existing four-paint V5 original verified byte-for-byte equal to V6
  on **the same detached native IR**.
- `W*` split from `n` across two Content Streams, with a distinct Clip
  definition CTM from image painting.
- Full-page direct original-PDFium RGB tolerance, red/green/blue/magenta/
  yellow/white interior goldens and byte-exact repeat Native RGBA after
  destruction of qpdf/source owners and the owned IR.
- Exact fail-closed output on absent/early `Q`, all-clipped or
  all-unclipped paint, missing Image/Path, more than two of either,
  extra/late unqualified operations, unsupported Stroke/CTM/SMask,
  missing resources and source capacity exhaustion, followed by
  recovered valid source rendering.

This is **generated** source-PDF conformance only, not admission of new
independently licensed externally published originals. Formal external
corpus remains **7 unchanged originals / 2 independent `W*`
pixel-positive PDFs**; none of those figures are inflated by test
replays or this feature.

## Remaining boundaries

This is not arbitrary Path/Image painting: only the existing 2–4 layer
bounded mixed C11 profile, **one** simple W/W* Clip leaf, integer
axis-aligned CTMs, opaque Path, normalized RAW image resource content and
fixed alpha policy are admitted. It does not render source text/fonts,
Form transparency groups, blend/soft mask, Image interpolation, ICC/CMYK,
general Stroke/Clip DAGs or arbitrary page boxes.

The production public page renderer/thumbnail remains **PDFium-backed**.
No runtime, SDK import/link/install or package removal is qualified here.
Keep #427, #145, #154, #214 and #208 OPEN until the broader actual
renderer feature and cutover gates are proven.
