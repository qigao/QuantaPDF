# Source V10: lossless ten-command nested W* rectangle normalization

**Parent:** [Draft #554](https://github.com/qigao/QuantaPDF/pull/554), qualified exact HEAD `cf9cdf5564b98fc57b34a775a8f1b4c3ab3db888`.

## Immutable original evidence

The unmodified, 3,747-byte original `tests/external_source_corpus/documents/yworks.clippath-cliprule-v1.pdf` (raw SHA-256 `9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783`) includes an initial W* Clip with **eleven authored PDF path commands**: outer `m+4l` (last `l` returns to `m`) and inner `m+4l+h` (last `l` returns to `m`). Naively adding the outer implicit CLOSE would yield twelve internal commands, not ten. Replacing each final returned edge by its equivalent `CLOSE` keeps both real closing edges and yields exactly **10 internal commands**: `M L L L CLOSE; M L L L CLOSE`.

This is a geometry-preserving representation change, not a dropped edge, unclipped fast path or new renderer.

## Strict one-leaf Source proof

Only the existing Source V10 parser recognizes an authored first complete four-edge **axis-aligned rectangle returned to its own starting point**, followed by a second, complete nested four-edge rectangle with returned endpoint and an explicit `h W* n`. The parser proves transformed coordinates, nonempty strictly nested bounds, one W* winding rule, exactly two contours, complete original stream and the exact ten-command canonical representation before publishing an owned Clip.

The ordinary nine-command W/W* path ceiling remains intact, and the new ten-command shape is *only* accepted through an independently revalidated narrow Source/Device/V5 path. Device projection uses the existing bounded command projection with one extra fixed stack slot; the V5→V1 raster is the same canonical 4×4 C11 evaluator, with its original pixel and sample budgets. Neither a fallback, PDFium runtime, extra parser/Renderer nor a second pixel owner exists.

## Original PDF input acceptance

The existing `quantapdf.native_v10_wstar_compound` test now authors actual original PDF operators (via the established QPDF test writer) for two new public C11 pixel-positive W* ring/hole cases. Four additional strict negative source PDFs prove wrong W winding, third contour, an inner rectangle extending past the outer, and late text all return an empty Bitmap. The old 11 RGB positive / 13 negative cases and independent historic compound/sibling OOM tests remain unmodified. Expected combined case count is **13 positives / 17 atomic negatives**. No new installer, consumer, CI or test target is generated.

## Scope still unqualified

The original yWorks PDF also has an enclosing W, three independently restored W*/W/W* Clip scopes, a first 1px red Stroke, then two blue Fills, all under authored q/Q and a negative-Y CTM. **This PR is just a first one-leaf W* source qualification.** It does not claim full yWorks public Source support; the unchanged external original remains **UNSUPPORTED/null Bitmap**. Materializing the genuine four Clip leaves, three intersection nodes, three GS identities, and all three paints against the original 8,000,000 global work cap is a separate gate under #213/#214/#233. Keep this PR and its parent stack Draft/unmerged.
