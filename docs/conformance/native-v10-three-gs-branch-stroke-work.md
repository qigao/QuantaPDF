# Original-shaped C11 three-GS Stroke-first branch — private quota proof

Stacked on qualified [Draft PR #552](https://github.com/qigao/QuantaPDF/pull/552).
The unmodified original yWorks `clippath-cliprule.pdf` is 3,747 bytes
(SHA-256 `9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783`).
Its page content has one red horizontal `S` Stroke, then two blue
`f` Fills, under a common W plus independent W*/W/W* Clip scopes.
Public Source V10 still rejects it; this new test is **original-shaped IR**,
not evidence of actual Source parser admission or original RGB.

An explicit *private* V5 admission borrows exactly three opaque Normal
GS and seven owned frozen Clip nodes, roots 5={1,2},6={1,3},7={1,4}.
GS1 paints an open 1px horizontal/vertical butt segment, GS2/GS3 each
paint one original-shaped bounded opaque Fill. The first nested Clip is
one **ten-command** two-rectangle W* path; earlier V5 entrypoints retain
their original nine-command restriction. All source and resource quotas,
no extra/hidden pointers, no masks/images/transparency/unknown paints,
and immutable detached output are preserved. No new parser/renderer,
PDFium runtime/oracle/fallback or public SDK ABI.

The SAME C11 V1 4x4 sampler's strict Stroke opt-in computes its
flattened one-segment AABB **expanded by 0.5 device pixel on both
axes**, then intersects both Clip AABBs before charging every
visited 16 subpixel × (1 Stroke edge + 4 outer + 8 W* edges).
At 600x600, original-style red line 30..400 at y40, first W*
outer rectangle x30..400 y10..80 and hole x50..150 y20..70:
`370 × 2 × 16 × 13 = 153,920` conservative checks.
Second and third synthetic Fill controls charge 69,120 and
57,600. The unchanged aggregate 8,000,000 quota is partitioned into
three fixed shares with no unspent-work transfer:
**461,760** admits while **461,759** rejects atomically.
First red Stroke has analytically half-red/half-white pixel `ff7f7f` (V1 alpha 128 composed by V5 over white)
at x40/y40; inside its W* hole is white, while switching to
nonzero winding gives the same half-red there.

Acceptance includes exact RGBA pixels (red half-width / W* hole,
blue and green independent branch Fill controls), byte-exact repeat,
detached ownership, original old-entry refusal, strict malformed
GS/Clip/style/transform/edge/pixel/sample-budget negative outcomes,
full zero-output on failure and Linux ASan/UBSan + Windows/macOS.
This is *not* original external PDF pixel qualification: remaining
gates are authentic Source parser q/Q + four-leaf/three-root IR
ownership, implicit subpath closure, actual CTMs and independent
full-page original-source RGB. Keep #213/#214/#233 OPEN.
