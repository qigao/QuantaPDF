# Native embedded TrueType text M0

This draft extends the existing complete Source V10 → owned IR → same C11
V1 raster → public Bitmap route. It reuses `qpdf_source_text.cpp`'s existing
font/CMap/text-state interpreter on the original Source callback stream.
There is one PDF tokenization, no retry or second PDF parser, system font
substitution, font bytecode execution, provider image or fallback renderer.

## Qualified scope and limits

| Part | Initial scope |
| --- | --- |
| Page | Text only; integer, zero-origin full MediaBox; Rotate 0; UserUnit 1 |
| Font | One embedded Type0 Identity-H CIDFontType2 with FontFile2 TrueType glyf/loca and ToUnicode |
| Mapping | Identity or bounded explicit CIDToGIDMap; original PDF DW/W advances |
| State | Positive axis-aligned text/CTM scale, spacing, rise, Tm/Td/TD/T*, Tj/TJ and quote operators, balanced q/Q |
| Paint | Fill text (Tr 0), opaque DeviceGray/DeviceRGB, original order; maximum four text-show operators |
| Glyphs | 64 source glyphs total; 4096 contour commands; 8192 points and 128 expansion visits per glyph, 16 levels, 32 components per node |
| Composite | XY-positioned components with bounded transforms; no point attachment or variation fonts; unhinted offsets retain fractional coordinates |
| Device | Physical 72/96/144 DPI, original coordinates × DPI/72; ceil buffer, actual fractional fringe intersection |
| Raster | Same deterministic 4×4 nonzero coverage and integer source-over, separately for each glyph |
| Existing caps | 2 MiB original PDF, 64 KiB decoded Contents, 512 source tokens, four original paints, 512 admission edges, 1M output pixels, 8M aggregate work |

A TJ array is one logical text-show paint. Glyph expansion has its own 64-glyph
bound; it does not widen the four original paint limit for paths/images.
Spaces still count as source glyphs and advance using original PDF metrics.
Empty glyphs emit no ink. Each nonempty glyph remains a separate paint:
merging glyph contours changes antialiased overlap, even for one Tj.

The existing font table reader now decodes bounded simple and composite glyf
outlines. The source interpreter resolves implied on-curve points, splits
quadratics at Y extrema and emits owned cubic contours. ROUND_XY_TO_GRID
is a device-grid hint, not applied in this explicitly unhinted profile.
Original quadratic and stored cubic reference results agree on every tested
sample; this is a bounded conformance claim, not a universal numerical proof.

The same C11 renderer first admits all contours/edges. Exact line predicates
retain product/sum residuals, including sample ties. Cubic ray classification
uses outward intervals and bounded monotone root subdivision; an unresolved
sample refuses the complete page. Every visited command, ray comparison,
interval node, event comparison and written covered sample is billed. Only a
completed temporary RGBA owner is published. Later unsupported text/graphics,
malformed fonts/maps, exhausted quotas and allocation failures discard pixels.

## Evidence

`tests/native/native_ttf_fixture.h` authors original input PDFs using the
already reviewed Apache-2.0 Roboto fixture. Fixed glyph IDs and original PDF
widths are independent of the production decoder. These are generated
conformance inputs, **not additional external-corpus originals**.

`tools/qualification/native_ttf_reference.py` uses fontTools solely to read
original quadratic glyf contours; 60-digit Decimal quadratic roots classify
physical samples directly. It does not import the SDK, convert quadratics to
production cubics, flatten edges or reuse production pixels. Checked-in RGBA
references cover Hello, three overlapping colored shows including composite
é and same-Tj negative-character-spacing overlap, TJ displacements and text crossing right/bottom physical page edges at
all three DPIs. CI uses the bytes without a fontTools dependency.

The existing public native test compares every RGB byte, checks explicit
CID mapping, text matrices, rise, both quote operators, spaces, repeat and
thumbnail rendering, post-Document/Context Bitmap ownership, and text
extraction/structured spans/search. Twenty malformed/unsupported originals
refuse at all three DPIs and through thumbnails; a good original reopens on
the same Context afterward.

The existing Source/OOM test checks detached owned glyph IR, Source stage-8
C++ allocation failure, all nine actual Source C allocations, four actual
native C allocations and retry, exact N/N−1 aggregate work, minimum edge
admission/one-below, pixel and logical operation limits, source immutability
and retained pixel ownership. No targets, workflows, consumer projects,
public ABI or external-corpus provenance records are added or changed.

Full exact-HEAD cross-platform CI evidence is recorded on the draft PR.

## Still unsupported

Mixed text/path/image pages; nonembedded/Base14, Type1, CFF/CFF2, simple
TrueType font encodings, vertical writing, point-attached composites,
variable fonts, font hinting, text strokes/clipping/rendering modes other
than fill, transparency, rotated/sheared/reflected text matrices, noninteger
or cropped/rotated/UserUnit pages. Unicode extraction is not shaping or
bidirectional layout support. General PDF text rendering remains open.
