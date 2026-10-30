# Native V10: external original clipped one-pixel Stroke

[Issue #233](https://github.com/qigao/QuantaPDF/issues/233) and
[Issue #213](https://github.com/qigao/QuantaPDF/issues/213).

## Unchanged original source, strict pixel proof

The original upstream test PDF
`tests/external_source_corpus/documents/pdfium.clip-path-v1.pdf`
is an **unaltered 514-byte** BSD-3-Clause reviewed source at
SHA-256 `6674dbd092ef8ae55b6ab1cda6a8b966019fbf22f7556291253853303072288b`,
with license/attribution review in #260. Existing offline admission
verifies its source revision, recorded rights and exact hash before
*any* renderer invocation. This is an external source, never a test
fabrication.

Its single original 100×50 page creates the Clip
`10 15 m 40 15 l 40 35 l 10 35 l W n`, sets blue stroking
`0 0 1 RG`, then Strokes the implicitly closed triangle
`10 10 m 25 40 l 40 10 l s`. The Clip excludes the triangle's
miter joins, so the **existing** C11 V1 one-pixel segment sampler
has already proven this restricted combination geometrically.

The public Native V10 acceptance demands 100×50 RGB8 source pixels,
white outside the actual Clip, visible blue ink on its two clipped
segments (at least 15 blue-dominant pixels) and no other unexpected
colors. Repeated original/staged equivalent samples must be byte
identical. A Bitmap remains readable after all other source Documents
and Context retirement. No PDFium pixel oracle or alternate renderer
is used.

## Exact production admission

Source V10 stages just one opaque, non-dashed, one-unit-width,
butt-capped, miter-joined **closed 3-edge Stroke** under one bounded
nonzero `W n` original Clip. Exactly three authored MOVE/LINE/LINE
points and the original `s` implicit closure are required. The
actual Source graphics state must have exact DeviceRGB channels,
`width=1`, `cap=0`, `join=0`, `miter=10`, an empty dash
pattern and no observed `q/Q` Stroke-state scope. Original
`G`/`K` setters cannot masquerade as a retained old `RG`
color; unknown CMYK, hairline, round caps, bevel joins, dashes,
non-unit CTM, non-1 /UserUnit, nonisometric device DPI, unclipped,
open, additional and late Text/Stroke operators still
fail with NULL Bitmap.

No Stroke state object, new renderer or Source fallback is created.
Source stages this as an owned PATH carrying the original blue Stroke
color and the **existing** single frozen Clip/GS resource. C11 V5
device projection permits only this one-stroke shape to reach the
**same existing C11 V1 clipped triangle sampler**. The same
geometric guard rejects any miter join that could contribute pixels
inside the Clip; the same 4×4 sample-work and edge budgets apply.

This is *not* general Stroke support, and it does not broaden older
private Source V1–V9 profiles, image semantics or public ABI.

## External corpus reconciliation

The child branch adjusts the prior #545/#546 external negative replay:
`pdfium.clip-path-v1` becomes a genuinely rendered **public-positive**
source verified by its own exact original CTest, not a historical
Graph/Composer claim. The remaining two currently unsupported reviewed
external originals are `yworks.clippath-cliprule-v1` and
`texample.intersecting-rings-v1`.

Those two are still strictly refused under the 72 DPI public V10
profile, with an independent reopened/retired external blank page
after each rejection: three seeded deterministic passes yield
**6 null-Bitmap refusals and 6 complete white recovery paints**.
Only two permutations exist for two IDs, so the third seeded pass
repeats canonical order and the independent receipt checker requires
the exact seed, order and fingerprints without falsely claiming three
different orders. All **seven original external files** remain
SHA-256/provenance verified; the external manifest's historical
private-profile classification remains unchanged.

Progress is always qualified against the exact stacked PR HEAD
and Linux Release/ASan+UBSan, macOS, Windows and Installed C11 gates.
Keep the dependency chain Draft and #233/#213/#214 open.
