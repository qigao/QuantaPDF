# TeXample original: first fractional-width C11 Stroke slice

**Parent:** exact-head qualified [Draft PR #558](https://github.com/qigao/QuantaPDF/pull/558), HEAD `ec9bd7992eee69bb27c75ade19ce88bfdc18a6e6`, Native CI #41027412528 SUCCESS 3/3.

## Unmodified original TeXample source inventory

Source document: `tests/external_source_corpus/documents/texample.intersecting-rings-v1.pdf`, 1,396 bytes, SHA-256 `f691ff46e64353ad285a5ddf24c82b956de67b5a6b3ed7d244167a301f36a284` (original licensed CC BY-SA 4.0; no byte changes). Its actual Flate-decoded PDF contents, **not a generated approximation**, begin with `1 0 0 1 118.566 90.22 cm`, `0.3985 w`, save/restore `q/Q`, and contain two multi-cubic closed rings in a compound `W* n` Clip, an opaque orange `1 0.5 0 rg ... f*` ring, followed by two original black `S` painted rings. Each of six ring contours contains four cubic `c` operators. Numerous extra `m` commands create empty subpaths, which must not be mistaken for painted geometry or silently discarded from accounting. Original MediaBox is `[0 0 237.133 180.44]`, **not** quarter-grid integer source geometry.

The original requires more than the existing yWorks-specific Source graph: multiple separate cubic Bézier contours, compact but repeated empty subpaths, fractional page dimensions, fractional device stroke width, arbitrary-length flattening, a two-ring W* Clip, source-order Fill+two Strokes and q/Q state semantics.

## This PR: exactly one bounded private C11 width proof

Extend the **same production C11 V1 raster** and its existing test target, *without creating a second renderer, independent CTest/consumer target, installer, probe or workflow*:

- One new **private** opt-in is limited to a **single** two-command horizontal/vertical opaque butt-capped **0.3985f**-pixel width Stroke under exactly **two** borrowed and separately frozen Clip PATH leaves. Both leaf paths are flattened by the preexisting V1 path interpreter, with no duplicate mask logic. The original one-pixel API remains strictly 1px and continues to reject `0.3985f`.
- Preserve the original 4×4 subpixel grid and source-over composition; compute centerline distance with radius `0.3985f/2` in device units, not a hardcoded 0.5. Expand conservative sample bounds by this **same actual** radius on *both axes* before intersecting with **both** Clip envelopes. Bill every flattened paint/Clip edge at every visited 4×4 sample; **no raise** of the 8,000,000 global work budget.
- Reuse existing `quantapdf_native_three_gs_branch_stroke_work` CTest, borrowing its ten-command nested W* Clip and outer full-page W. Analytic RGB controls prove two rows of 4/16 red samples `FFBFBF` on opaque white, white outside the butt endpoints and in the W* hole, and byte-identical output after repeated rendering. Conservative work: `370*2*16*(1+4+8)=153,920` checks; **153,920 accepts** and **153,919 rejects atomically**. A 12-edge lease (needs 13) also refuses. Other widths, round caps, diagonal strokes, a single Clip, and the unchanged one-pixel entry refuse with zero image.
- The new private API is NOT routed by Source V10 and cannot publish the original TeXample as supported. Existing public **TeXample `UNSUPPORTED/null Bitmap`** and all yWorks positive/corpus-reconciliation counts remain unchanged.

## Separate strict follow-ups

1. Qualify original MediaBox fractional source geometry and exact original translate/CTM, without altering the old quarter-grid profile.
2. Normalize only proven empty `m` subpaths and represent all four cubic segments per ring, with an explicit path/edge and flattened-work lease, independent of the legacy nine-command grammar.
3. Preserve the original W* two-ring Clip, orange even-odd `f*`, **two** curved black `S` paints, and q/Q restore. General joins, color/pattern resources and dash state remain fail-closed unless independently proven.
4. Compare actual unmodified original PDF through public 72-DPI RGB to independently validated pixel and alpha references; test work-budget N−1, C allocation OOM rollback, explicit unsupported variants, lifetime, and external corpus receipt without changing historical source hashes or positive counts prematurely.

Keep related issues #213/#214/#233/#530 OPEN, this PR Draft/unmerged, master unchanged.
