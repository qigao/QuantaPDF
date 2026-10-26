# Native Source Scoped Mixed V5

Tracks [#427](https://github.com/qigao/QuantaPDF/issues/427),
[#154](https://github.com/qigao/QuantaPDF/issues/154) and
[#208](https://github.com/qigao/QuantaPDF/issues/208).

## Exact admission

The private, opt-in `quantapdf_qpdf_source_scoped_mixed_v5_to_ir()`
reads **all operators** of the unchanged original source PDF, then
returns uniquely owned Composer IR for the new generic
`quantapdf_ir_raster_scoped_mixed_v5()` pure-C11 renderer.

Its first source profile admits one, and only one, W or W* Clip PATH
with source CTM frozen at its construction, followed by **exactly four
source-ordered paints**. There must be **one 8-bit Gray/RGB RAW Image
XObject `Do` and one opaque RGB-filled PATH while W/W* is active**,
followed by a **real `Q` pop restoring the no-Clip state**, followed by
one more Image `Do` and one Path fill. Within either scope, Path and
Image may paint in either order (four sequence combinations).

The private source materializer copies all normalized RAW image pixels,
opaque Path geometry, original source order and the one Clip leaf into
owned Composer resources and operations. Every operation has an explicit
graphics-state ID: 1 for frozen W/W* scope, 0 for restored/unclipped
scope. Original PDF/qpdf parser and source storage do not survive into
the C11 renderer.

Balanced q/Q, axis-aligned non-negative integer CTMs, bounded page
geometry, selected PageTree and inherited resources, direct/Flate RAW
Image XObjects, normal blend, full GS alpha and exact RGB 0/1 path fill
are mandatory. All unknown operators or unadmitted text, shading,
Stroke, soft mask, Form, inline image, group/color-space/annotation,
second Clip, missing resource, invalid CTM, source/paint budget overrun,
incorrect operation cardinality, missing Q restore or malformed source
cause an **atomic error** with no partial owned IR.

Earlier private Source V1–V4 signatures retain their narrower admitted
operator/paint cardinalities. V5 does not promote old source cases or
silently use any fallback.

## Qualification

The source test creates a test-only QPDF-authored PDF with two
independent Image Do operations and two Path paints. It compares
Native C11 output against **PDFium rendering the unchanged original
document directly**, not against a reserialized IR. Full RGB metrics,
interior red/green/blue/yellow/white positions, exact repeated RGBA
and source-owner disposal are checked for four source layer orders.
Additional cases exercise W*, Clip definition CTM, W*/n split across
two Content Streams, later unsupported content/CTM/resource/budget
failures, and successful rendering after refusals.

The generic V5 Raster target remains pure C11 with **zero PDFium/qpdf
linkage**. Only the original-source test links qpdf and PDFium for source
construction/parsing and the independent test oracle.

## Limitations

This is a **strict four-operation subset**, not arbitrary mixed PDF
page painting. Native V5 still caps layer work and allows only one
frozen Clip leaf; this source bridge intentionally excludes Stroke,
general colors, font outlines, image masks, patterns, transparency
groups, arbitrary transforms and general PDF source content.
No production `quantapdf_render_page()` dispatch, public ABI,
installed SDK import or package dependency changes. Formal licensed
external original sources remain **7 admitted PDFs / 2 W* Native-positive**;
QPDF-generated tests never enter that count. Full production PDFium
removal remains gated by #208.
