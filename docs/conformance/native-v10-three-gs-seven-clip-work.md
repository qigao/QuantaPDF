# Native V10 private three-GS / seven-Clip cumulative-work increment

This is an internal opt-in stacked on qualified [Draft #550](https://github.com/qigao/QuantaPDF/pull/550).
The original 3,747-byte yWorks `clippath-cliprule.pdf` and TeXample
remain **UNSUPPORTED/null Bitmap** in the public Source V10 corpus.

The immutable, borrowed pure-C11 IR contains exactly three opaque PATH
Fill operations with GS IDs 1,2,3 and exactly four Clip PATH leaves
(one or two complete closed contours, <=9 commands, frozen identity CTM),
plus intersections 5={1,2}, 6={3,4}, 7={5,6}. GS Clip IDs are 1,5,7,
respectively. No images, unsupported resources, Stroke, alpha, fallback
provider, second parser or second raster. Integer viewport avoids a
fifth geometry-mask slot. All input resources and capacities are
validated before output publication; detached output remains immutable.

Same existing V5 compositor and V1 4x4 winding evaluator.
Each of the three PATH layers consumes at most
`floor(max_sample_tests/3)` and `floor(max_edges/3)`.
The sum of the three ceilings can never exceed the original global
8,000,000 sample cap; unused per-layer quota is not transferable.

Pure-C11 600x600 fixtures must prove red/blue/green GS identities,
the W* hole retaining red, nonzero versus even-odd winding, repeated
identical bytes, source ownership isolation, rejection with zero outputs
and exact cap 614400 accepts versus 614399 rejects. Conservative
upper bills: layer 1 = 204800, layer 2 = 163072, layer 3 <= 128128.
Legacy two-GS APIs reject the same three-GS IR.

This is **not** yet proven to match the actual original yWorks
three-GS/seven-node resource layout. Next Source V10 qualification
requires the real original's graph, frozen state/CTM semantics,
independent source RGB controls, W/W* negatives and unchanged
cumulative quotas. Keep #233/#214 OPEN and stacked PRs Draft/unmerged.
