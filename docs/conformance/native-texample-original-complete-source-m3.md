# TeXample Source M3 — complete original 32-cubic ownership, private only

Parent: [Draft #561](https://github.com/qigao/QuantaPDF/pull/561), exact HEAD `33f70ab29a8149f39fe9cbcce6b99002fa44f091`, CI #41035388512 **SUCCESS 3/3**. Authored against the **unchanged 1,396-byte** TeXample original PDF `tests/external_source_corpus/documents/texample.intersecting-rings-v1.pdf` (SHA256 `f691ff46e64353ad285a5ddf24c82b956de67b5a6b3ed7d244167a301f36a284`). Original original PDF bytes and all external provenance/manifest records remain unmodified.

## Full decoded original content and no-edge ownership

The actual original Flate content is exactly `1 0 0 1 118.566 90.22 cm`, then four saved `q` and restored `Q` across scopes, one `0 G`, one `0 g`, one `0.3985 w`, one `1 0.5 0 rg`, one `W* n`, one orange `f*`, two separately scoped black `S`, and final `n`. **Twenty-four authored `m`**, **thirty-two authored `c`**, **eight authored `h`**, divided into **four groups of two four-cubic rings**. Each group's six `m` includes two actual four-edge ring starts and **four zero-edge moves** (three overwritten by later `m`, one terminal unpainted `m`). Thus exactly **16** `m` moves do not contribute a stroked or filled edge, but every operator and the source cap are explicitly counted.

The private TeXample M3 bit is set **only** by a new internal Source entry point. That entry reuses the *identical* production V10 QPDF PageTree, original content parser, `handleObject` operator stream, CTM, and fixed Source IR allocation owner. The ordinary **public V10** selected reader never sets this flag and retains its strict unsupported behavior.

Only under the exact literal original decimal MediaBox `[0 0 237.133 180.44]` (M2), original `1 0 0 1 118.566 90.22 cm`, zero-origin page, original `/Rotate 0` and `/UserUnit 1`, private M3 qualifies a path coordinate from the unchanged five-decimal source; top-left Y-down staging computes `(118.566+x, 180.44-90.22-y)` with bounded original coordinates, not a 238x181 substitution or a second parser. It does not quantize cubic control points into quarter-grid or approximate ring geometry by boxes; float32 is the existing Composer command representation.

## Strict complete-page Source materialization

At EOF (and **only** after every original operator, original `q/Q` balance, exact 24/32/8/16 path/move counts, original paint order and page/byte bounds pass), return one fully C-owned page IR:

- Original page remains physically **237.133 × 180.44** points, represented by the existing Composer float32 fields, with no substituted 238x181 pixel canvas.
- Exactly **one** frozen `W*` Clip PATH (12 commands, `M+4C+CLOSE` twice), from the original first two concentric rings.
- Exactly **three** ordered PATH paints, **12 owned commands each**: orange even-odd `f*` under the Clip's Normal/opaque GS1, then black `S` left and black `S` right as independent GS0 unclipped paints, each retaining original `0.3985f` stroke width.
- Exactly **1 GS** and **0 Image/Form/Font/other hidden paints**; all 32 original cubic Bézier commands represented as 4×8 fully owned CUBIC_TO values.

Extend ONLY the existing Source-stage/OOM test and the existing CTest invocation with the unchanged TeXample PDF path. The actual original is independently parsed by both the new private entry and the historic public selected entry: private Source M3 requires complete exact IR, public Source must still return **UNSUPPORTED/null IR**. Inject each of the actual C allocations and require **NOMEM, all-zero IR, then successful independent reparse**. No test-only replacement parser, fallback, artificial source input, installer/consumer project or new CMake/CI workflow.

## Product-level nonclaims and later gates

**No new public TeXample RGB claim.** The first two original W* Bézier rings have independently passed the previous private V1 4×4 winding test (#560), and one narrow fractional open-line Stroke width under two Clips passed #559. Neither test proves the actual **closed curved 0.3985pt S** on the remaining four contours, the real orange f*/two-S layer work under W*, join handling or fractional final pixel row/column.

The original remains **public `UNSUPPORTED/null Bitmap`**. Next, independently qualify physical decimal 72-DPI device projection, *all three* render layers using the single C11 V5→V1 sampler, exact original RGB, per-layer and total 8,000,000 sampled-edge cap and N−1 refusal, fail-closed unsupported variants, and external original corpus reconciliation. Keep Issues #213/#214/#233/#530 OPEN; parent stack Draft/unmerged and master untouched.
