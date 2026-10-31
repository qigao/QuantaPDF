# Native V10 two-GS visible-work budget, not yWorks admission

This independent, stacked Draft increment follows qualified
[PR #549](https://github.com/qigao/QuantaPDF/pull/549), as one bounded
step toward the unchanged, licensed and SHA-256-reviewed 3,747-byte
yWorks `clippath-cliprule.pdf` original. The original **remains**
`UNSUPPORTED` in the public Source V10 external corpus. A generated
pure-C11 test is never counted as a fourth externally sourced positive.

## Exact accepted shape

The existing owned C11 Clip resource graph consists of exactly three
nodes: two frozen Clip PATH leaves and **one flat intersection root**
whose members are `{1,2}`. A single source-owned resource registry
contains exactly **two** opaque Normal GS records: `GS1 -> leaf1`
and `GS2 -> root3`. Exactly **two** source-order PATH Fills, with
operation GS IDs `1,2`, use the existing V5 source-over compositor
and V1 shared 4×4 subpixel winding sampler. Each Clip leaf may have
one or two closed contours within the original **nine-command** cap.
No extra operation, XObject, image, GS, Clip root, font, mask, stroke or
late unknown operator is silently discarded.

This increment is a **private C11 opt-in**, not a public SDK ABI
addition or a new standalone PDF parser/renderer. No Source V10
page-reader graph extension has been claimed. Other calls (including
the historical compound two-GS C11 entry) retain their full-page
work budget and existing failure status.

## Conservative aggregate sampling proof

The previous single-leaf/Fill visible-work proof extended in #549
is used **inside the same V1 C11 implementation**, separately on each
of the two fully admitted PATH layers. V5 already partitions the
original `max_sample_tests` by the number of edge-consuming layers:
for exactly two Fills each receives at most
`floor(max_sample_tests / 2)`. Therefore the **sum of both accepted
layer work upper bounds never exceeds the original total**. This is
conservative: unused work from one layer is not transferred to the
other; it does not raise the 8,000,000 ceiling.

At 600×600, the C11 acceptance fixture paints a 40×40 red rectangle
under leaf1, then a blue rectangle under the frozen intersection of
leaf1 with a two-triangle W* Clip. The first layer has
`(4 PATH edges + 4 Clip edges) × 16 samples × 40 × 40 =
204,800` conservatively billed edge checks. The second layer has
`(4 PATH + 4 leaf1 + 6 leaf2) × 16 × 26 × 28 =
163,072`. Thus a total cap of `409,600` accepts (each layer gets
204,800), while `409,599` must refuse with a completely zeroed
output. The unmodified 8M cap also accepts this explicitly qualified
profile, but the historical full-page two-GS API must still reject
the exact same source.

The pure C11 CTest validates every channel at specific red/blue
interior and even-odd hole control pixels, untouched white corners,
identical repeat bytes, two-state source-over order and the difference
between nonzero and even-odd winding in leaf2. It also rejects
inverted/unmatched GS identifiers, bad root members, bad Clip
capacity/geometry, hidden image ownership, opaque alpha changes,
stroke, third operation and low pixel quota. Every negative must
publish a fully zeroed, unowned output.

## Remaining actual external source gap

The unchanged yWorks original has **three paints**, **seven Clip nodes,
four leaf Clips (two W and two W*)**, and **three Graphics States**
on a 600×600 page. This two-GS / 3-node budget proof is not equivalent
to that original graph. Next prerequisites are a distinct bounded
seven-node/three-GS source admission, immutable per-operation
Clip membership, provably sufficient **aggregate** work accounting
under the same 8M ceiling, and exact public C11 RGB goldens on the
original unmodified PDF. Do not use old private Graph/PDFium reference
parity as public V10 success. TeXample remains separate and
`UNSUPPORTED`. Keep all stacked PRs Draft and #233/#214 open.
