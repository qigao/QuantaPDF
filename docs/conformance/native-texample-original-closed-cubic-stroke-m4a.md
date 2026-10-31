# TeXample original M4a: first real closed 0.3985pt Bézier Stroke in one C11 renderer

Parent: [qualified Draft #562](https://github.com/qigao/QuantaPDF/pull/562), HEAD `21893b71860643e1164ee88f0200209237d786c6`, Native CI #41062154325 SUCCESS 3/3. This changes NO original PDF, source QPDF parser, public Source raster bridge, installed SDK exports or CI/consumer project.

## Original Source evidence and exact bound

The unchanged 1,396-byte `tests/external_source_corpus/documents/texample.intersecting-rings-v1.pdf`, SHA256 `f691ff46e64353ad285a5ddf24c82b956de67b5a6b3ed7d244167a301f36a284`, contains **24 m, 32 c, 8 h**, original MediaBox `237.133 × 180.44`, one translated `1 0 0 1 118.566 90.22 cm`, `W* n`, orange `f*`, then **two closed black Bézier `S` paints** with original `0.3985 w`. Private #562 independently proved the ENTIRE real Source M3 graph and every original Source C allocation fault. This PR reuses that exact source-owned IR, *without* synthesizing a ring or obtaining a PDFium image.

For this **PRIVATE M4a slice only**, form a shallow one-operation local IR borrowing each of the two real black Stroke paths with its GS0 but replacing the page geometry with a **238×181 integer sample canvas**. The original Source command coordinates remain **unscaled original physical points**; this is a bounded C11 test fixture and DOES NOT claim true fractional public Page/DevicePlan correctness or full original pixel parity.

## Same V1 4×4 kernel, independently qualified curved mode

One named private V1 entry accepts exactly one original closed two-ring PATH of **12 commands, MOVE + 4 CUBIC + CLOSE twice**. Require both nondegenerate strict nested concentric cardinal rings, bounded original cubic controls, four matching tangential knot joins per ring, **0.3985f** opaque black Stroke, solid butt cap, default miter join/limit, GS0, and untransformed integer private sample canvas. Neither arbitrary widths, dashed paths, open lines, malformed geometry, extra resources/Clip nor more than one paint qualifies.

The new opt-in uses the existing SAME V1 de Casteljau .125px cubic flattener and **same 4×4 point-to-segment distance**, straight-alpha blending and detached pixel allocator. Since a full rectangle bbox × all 192 flattened edges would be a falsely prohibitive cost, it constructs a conservative AABB for **each** actual flattened edge inflated by **0.3985f/2** on both axes, then bills **all 16 subpixel edge-distance tests for every pixel** in that edge's candidate box BEFORE allocating output. Each pixel's existing Stroke sample branch examines only its pre-admitted local candidate edge set. Every potential tested segment is charged, including when an earlier one covers the sample. **Global 8,000,000 sample-test limit remains unchanged**, and every budget/metadata/allocation refusal clears output atomically. This is a work-index within ONE C11 renderer, not a second renderer or fallback.

## Existing test: actual PDF first/second black closed S

Extend existing `native_source_stage_oom` test only. Borrow each original black Stroke from the original #562 fully qualified Source IR and render separately to 238×181 with no rescaling. Verify an outer cardinal-knot subpixel edge is black-antialiased while center and outside are white; complete RGBA repeated output is bit-identical; first output survives destruction of its Source/IR owner. Exact private sample-work gate is determined by source's real flattened candidate-edge bills, requiring `N` admission and **`N−1` atomic UNSUPPORTED/zero output**. `max_edges=191` refuses (192 true flattened edges), modified width `0.4` and non-miter joins refuse. All original external public TeXample `UNSUPPORTED/null Bitmap`, yWorks public 72 DPI RGB and SHA/corpus reconciliations remain unchanged.

## Not yet original public rendering or exact PDF join parity

This milestone qualifies a true **closed cubic curve-width candidate** with original Source contours and bounded C11 pixel work. It does not yet prove all PDF miter join subtleties at *flattening subdivision vertices* or the correct **237.133×180.44** fractional page's real 72 DPI final fringe. Complete orange f*/first black S/second black S **source-over under W*** and conservative aggregate three-layer 8M work budget are separate gates. The untouched original TeXample must remain PUBLIC `UNSUPPORTED/null Bitmap` until those independent full-image RGB, source lifetime, OOM and corpus reconciliations pass.

Keep PR Draft/unmerged, original parent/ master unchanged, Issues #213/#214/#233/#530 OPEN.
