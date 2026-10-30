# TeXample original: private two-ring cubic W* native C11 milestone

Parent: [Draft #559](https://github.com/qigao/QuantaPDF/pull/559), exact HEAD `13b7d31afb416d2b4eced53ddb3579ac1bb4c8a3`, CI #41029715119 SUCCESS 3/3.
Original is the **unchanged** 1,396-byte `tests/external_source_corpus/documents/texample.intersecting-rings-v1.pdf`, SHA256 `f691ff46e64353ad285a5ddf24c82b956de67b5a6b3ed7d244167a301f36a284` (CC BY-SA 4.0).

## Corrected original decoded operator inventory

**Correction of the prior issue/docs shorthand:** the actual Flate content contains **eight** complete cubic contours, **four `c` operators in each: 32 original `c`**; it is NOT six contours/24 cubics.

- Initial nested **W*** Clip: 2 closed rings × (M, 4 cubic `c`, `h`) = 8 cubics, common source-space center `(-28.3468,0)`, inner/outer axis cardinal radii `56.69362` and `85.04042`.
- Orange **`f*`**: 2 concentric closed cubic rings = 8 cubics, second center `(28.3468,0)`; clips against the original W*.
- First black **`S`** (left): 2 closed rings = 8 cubics.
- Second black **`S`** (right): 2 closed rings = 8 cubics.

The content also has repeated **zero-edge `m`** subpaths before/between/after closed rings; they MUST be accounted for without introducing painted geometry or dropping nonempty source paths. Original `0.3985 w`, `1 0 0 1 118.566 90.22 cm`, `/MediaBox [0 0 237.133 180.44]`, `q/Q`, `W* n`, `f*`, `S`, `S` remain unchanged. The original page dimensions have three decimals, outside earlier Source quarter-grid geometry.

## Newly qualified PRIVATE C11 W* Clip slice

One newly named **private** V1 entry accepts **only** an already staged owned Clip PATH leaf with 12 commands: 
`M C C C C CLOSE; M C C C C CLOSE`. Each ring visits rightmost, top, leftmost, bottom, rightmost cardinal endpoints around the same center with strictly nested positive radii and controls within each bounding rectangle. Representational comparison tolerance **0.0001 source point** only validates equality of exact five-decimal authored cardinal endpoints after float32 source translation; it does **not** move, quantize or discard source geometry or change the sampled curves.

The entry uses the **same existing V1 recursive cubic Bézier subdivision**, the same 4×4 even-odd winding sampler and the same immutable C11 output/zero-on-failure contract. There is **no new raster engine or fallback**, no global budget increase, no public ABI, and the historical W/W* or nine-command Source admission remains strict.

Extend the existing `quantapdf_native_three_gs_branch_stroke_work` C11 test (NO new target/CI/consumer). Its ring Clip comes from the actual original first 8 `c` coordinates, converted by the exact original `tx=118.566`, `y=90.22-y_pdf` transform into the device-down stage of the eventual source. For this **PRIVATE** evaluator only, the 237.133×180.44 original is conservatively represented on a **238×181 ceil canvas**—this does NOT claim full original Source fractional-geometry support. A narrow **already owned** 11px blue Fill rectangle intersects the source Clip, exercising both the ring's blue annular band at top/bottom and the even-odd white central hole. No fake PDF or second raster is used as a reference; goldens derive from original contour geometry.

The existing .125-pixel De Casteljau flatness produces 4×16 inner and 4×32 outer segments: **192 Clip edges**. The Fill adds four: **196 total**. The conservative visible envelope is **11×171** pixels ×16 subpixels, giving **5,898,816** maximum sampled-edge checks. The native test must admit **5,898,816** and refuse **5,898,815** with zero output, reject `max_edges=195`, malformed/extra-command/colliding-ring/non-W* inputs, and prove full-image determinism and detached ownership. Existing 8,000,000 global cap remains unchanged. Follow exact CI for the actual qualified value; do not weaken this hard numerical gate to make a test pass.

## Separate missing original-source acceptance gates

1. Full original fractional MediaBox and translated CTM, with independent exact source decimal and native-device geometry.
2. Normalize **all** observed zero-edge `m` without losing the 8×4 real cubic segments or changing PDF path closure.
3. Admit original **W* then orange f* then two black closed curved S**, original 0.3985px Stroke width, join semantics and q/Q restoration under independently owned Clip/GS, with conservative total flattened-work proof.
4. Finally require unchanged original TeXample PUBLIC 72-DPI RGB, OOM recovery and SHA-pinned external corpus reconciliation. **Until then TeXample remains UNSUPPORTED/null Bitmap**; public yWorks remains qualified, not reclassified.

Keep #213/#214/#233/#530 OPEN, this PR Draft/unmerged, no downstream installer/consumer or CI/ABI edits, master untouched.
