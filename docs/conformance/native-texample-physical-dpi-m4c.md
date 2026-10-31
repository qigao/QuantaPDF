# Original TeXample M4c: real physical 96/144 DPI

Parent is qualified Draft #566, head `1e50d0fb819ea8e456405c4f77a387e87a9481ef`, tree `a1428e2bd2a71283be5af9d492e5b6bcaaaacfba`, Native CI #2218 / `41155072270` SUCCESS 3/3. That head qualifies public 72-DPI RGB and refuses 96/144 DPI. This change extends the same public Source V10 and C11 V1 renderer to original physical 96/144-DPI samples. All PRs stay Draft/unmerged, with #213/#214/#233/#530 open. Exact resulting-HEAD CI is required for public acceptance.

## Actual physical coordinates and original width

The original page remains **237.133 x 180.44 points**. A physical plan uses **DPI/72** for each geometric axis, including the actual 0.3985-point Stroke width. Exact checked rational ceil determines only the 317 x 241 (96 DPI) or 475 x 361 (144 DPI) allocation. Source owners, commands, GS/Clip IDs and original float bytes remain immutable. No integer-canvas fit, temporary float command projection, bitmap upscale, second parser, provider retry or alternative raster is introduced.

Positive scaling is applied to outward-rounded cubic control intervals, including an outward interval for the nonexact double 4/3 ratio. Radius bounds use the same scale interval before squaring. Row crossings that touch unresolved endpoint intervals refuse rather than choosing a side. The same original-cubic distance classifier, monotone crossing certificate, authored smooth-join proof and three temporary 16-bit masks are retained. Actual flattened admission geometry is also generated at target DPI, sharing only the two already-proven duplicate geometries.

The existing page-fringe control now checks every output pixel against exact integer inequalities at all three DPIs, with opaque and transparent backgrounds. It exposed an existing full-canvas rectangle shortcut that painted fractional physical fringe at 16/16. The shortcut now intersects coverage with the physical viewport's same 4x4 grid before the existing blend. FIT behavior and conservative work preflight remain unchanged. The 72-DPI controls additionally cover oversized and exact-buffer rectangles, with and without Clip; higher-DPI exact-buffer controls use the unclipped full-canvas path within the unchanged 8M cap.

## Bounded row intersection without repeated ray comparisons

Previously qualified 72-DPI APIs retain their outputs, work accounting and 768/767 or 384/383 edge contracts. The new explicit 96/144-DPI entry shares the same complete Source/resource/shape validation and the same C11 raster implementation. For each subpixel row, the already-certified at-most-eight crossings per path become sorted sample-column thresholds. A bounded merge intersects the two even-odd regions; repeated thresholds toggle parity together. Every threshold comparison and every written Fill sample is charged. Repeated per-pixel ray comparisons are absent from this execution, rather than performed without charging them. Stroke still charges every actual recursive distance node.

| DPI | Exact dimensions | Admission edges | Measured aggregate N | Global caps |
| --- | --- | --- | --- | --- |
| 72 (retained API) | 238 x 181 | 384 | 4684463 | 512 edges / 8000000 work |
| 96 | 317 x 241 | 416 | 719470 | 512 edges / 8000000 work |
| 144 | 475 x 361 | 512 | 1487438 | 512 edges / 8000000 work |

The public facade's one-million-pixel cap and all Source/operation/edge/work caps remain unchanged. Each higher-DPI native fixture requires exact N success, N-1 atomic zero-image/zero-work refusal, exact edge success and one-edge-less refusal, every real temporary/output allocation fault and complete golden recovery. The same existing Source/OOM CTest runs 27 malformed/geometry/resource/scope/quota controls per higher DPI, original Source snapshots and detached outputs surviving Source IR destruction. Its original 72-DPI and Source allocation fault coverage stays intact.

## Independent references and public corpus evidence

The existing offline mathematical reference now accepts optional DPI and storage mode, with optional little-endian sample-mask output. Historical output-only invocation still produces the identical 72-DPI golden. The frozen original decimal controls, polynomial ray roots and quintic stationary-distance roots remain independent of production interval/range logic. Authoring decimals and Source binary32 storage produce identical complete three-layer masks at each new DPI. References, hashes, exact sizes and numerical margins are documented in `tests/native/goldens/README.md`. The unchanged original PDF and its provenance manifest/notices are never rewritten.

The existing public original replay target compares every opaque RGB byte at 72/96/144 DPI for TeXample, in three seeded source orders. It retains independent 72/96/144-DPI original Bitmap owners through subsequent renders, Page/Document destruction and final Context destruction. yWorks remains qualified only at 72 DPI; its 96/144 requests still atomically refuse. No yWorks width expansion is inferred from the TeXample result.

The version-2 strict receipt counts **24 actual executions: 18 positives (three yWorks RGB + nine TeXample RGB + six independently reopened blank recoveries) and six yWorks higher-DPI refusals**. It records the exact per-document qualified DPI list, true RGB counts and reference identities. Independent Python reconciliation rechecks all seven original files/licenses and all three golden hashes, rejects stale version-1 receipts, forged DPI/counts, altered sizes/hashes and omitted lifetime evidence. Historical integrity-only metadata still contributes no fabricated render positives.

The public 96/144-DPI result is qualified only after the existing Linux Release/ASan, Windows shared /WX, macOS, all 59 CTests, installed consumer, exact exports and no-PDFium gates pass at the same tree. No target, workflow, CMake, ABI, consumer project, original source, global budget or fallback changes. Arbitrary curved joins/dashes, page geometry, additional paints and wider Clip/resource profiles remain independent work.
