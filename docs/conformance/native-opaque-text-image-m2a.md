# M2a — ordered opaque Text/Image original Source qualification

Tracking: [#578](https://github.com/qigao/QuantaPDF/issues/578), parent [#570](https://github.com/qigao/QuantaPDF/issues/570), broader [#208](https://github.com/qigao/QuantaPDF/issues/208). **Status: pre-admission fixture and acceptance contract, not M2 support.**

## Frozen M1 baseline

Production master `cfb93d382b97b6ee1198f7990fc4b860f2958513`, complete Git tree `7b5bbb1df0098cf061b2f5e9b72beba50d3782cc`, passed independent master Native CI #2286 (four native configs 60/60, installed consumers 1/1). The Source V10 compiler, page-wide ledger, actual four raster malloc calls/order, existing golden pixels, original Source profiles, public C11 ABI, Context lease and synchronous Composer boundary are not changed by the present pre-admission tests.

## The first admission

Two fully valid **original PDFs** authored by the existing `native_ttf_fixture.h`, using the existing embedded CID TrueType font, 2x2 8-bit RAW RGB Image XObject and deliberate pixel overlap:

| Mode | Original stream order | Current / intended state |
| --- | --- | --- |
| 49 | `q BT /F ... Tj ET Q` → `q cm /I Do Q` | UNSUPPORTED now → whole-image Text-under-Image positive |
| 50 | `q cm /I Do Q` → `q BT /F ... Tj ET Q` | UNSUPPORTED now → whole-image Image-under-Text positive |

M2a must make **both** orders positive together. A one-way specialization is not acceptable. Keep one page, one embedded-TTF Text show, one previously qualified RAW/Flate Gray/RGB image, opaque Normal and only the already bounded positioning semantics. Existing image-local save/restore has no new cross-paint Clip semantics. M2b owns multiple scoped graphics states, frozen Clip, interleaved PATH Fill, broader font-resource/matrix cases. M3 owns later device expansion; no implicit broadening of DPI, RGBA or canvas contracts.

## Code-level migration boundaries

1. **Original complete Source admission** — `src/backend/qpdf_native_image_ir.cpp` currently hands every token after first BT to `source_text_outlines`; BT currently requires `target.draws.empty()`; `text_outlines_complete` takes a Text-only early return. Establish a single source-ordered paint list that freezes each completed Text show and Image Do without a second parser or silently removed operators. No live QPDF/font handles enter IR. Reuse the existing `src/backend/qpdf_source_text.cpp` outline interpreter rather than inventing another TTF decoder.
2. **Owned IR and closure** — Each operation records its real kind and source order. All Image RAW bytes and glyph command contours have exactly one owner, copied or adopted only after whole-document EOF/resource/geometry validation. All reachable and unsupported source resources are checked, not discarded. Source preparation atomically adopts this IR together with its existing Core Context lease.
3. **One mixed compiled C11 page** — `native_prepared_page_v1.h` presently diverts any Text to `QP_NATIVE_PREPARED_TEXT`; `ir_raster_v1.c` Text geometry expands only PATH glyphs and rejects image resources. Introduce a specifically qualified ordered mixed execution route: Text's bounded glyph-expanded paints and Image placement run on one canonical device grid, source-over order and **single** page work/pixel/byte ledger. Avoid Text-only/full-page intermediary RGBA and Image-only/full-page intermediary RGBA compositing: separate full-page renders would create a second budget and extra pixel semantics. Keep authored operation quota distinct from up-to-64 emitted glyph draws and bill every expanded paint/sample.
4. **Publication and lifetime** — Failed admission/preparation/painting publishes zero Bitmap/pixels/receipt and leaves exact ownership balanced; successful detached output survives prepared-page disposal, document lifetime permitting. Same-Context retry after failure works. Earlier Source V8/V9 and V10 Text-only/PATH/Image/Clip/TeXample routes are unchanged.

## M2a acceptance checklist (all must pass before M2a is checked)

- [ ] Flip the **same actual fixture** modes 49 and 50 from current atomic refusals to successful Source→owned IR→one C11 compiled page→public Bitmap; no separate test-only renderer.
- [ ] Independent whole-image analytic/high-precision references at **72/96/144 DPI**; measure overlaps and opaque compositing in both orders, glyph edges, physical placement and no altered original TTF golden.
- [ ] One aggregate ledger: original paint/expanded glyph counts, 64-glyph and 4096-command caps, 512-edge cap, 1M output-pixel cap, 8M actual-work cap, source pixels counted once, Image placement charged, retained/temporary byte peaks; N/N−1.
- [ ] Full-EOF fail-closed negatives including unknown trailing Do, extra resource, Clip/Fill outside M2a, malformed font/images, forbidden matrices, exceeding operation/glyph/edge/pixel/work/byte budgets; no partial image.
- [ ] Real Source C++ allocation and C11 IR/geometry/raster injected failures (including late failure), same-Context recovery, Context/IR/page/Bitmap teardown and detached output survival.
- [ ] Existing originals, goldens, resource caps, raster malloc 1–4 faults, Source/OOM 40s and Context/OOM 30s gates are **unchanged**. No installer/consumer test projects or probe CMake/workflows, no PDFium, new ABI or public Composer render API.
- [ ] Exact HEAD, tree, existing Native CI all Linux Release/ASan-UBSan/Windows/macOS and installed consumer checks before merge; independent production master-push CI after merge.

The checked-in pre-admission test documents the **real current refusal** only; it is not an implementation or passing M2 capability. The parent M2 checkbox remains unchecked until M2b is separately accepted.

## Stage S — ordered original Source IR

This intermediate implementation sends the existing TTF outline interpreter
the same Source parser callbacks and reports one completed Text show plus one
qualified Image Do into the existing Source staging, in either source order.
Each materialized result uniquely owns every glyph outline and decoded Image
payload. The production prepared-page text route still refuses mixed Image
resources: there is **no new public C11 Bitmap positive yet**. The tests now
require complete original-owned IR in both orders and explicitly verify this
compiled-page refusal. Pixel/edge/operation quotas and existing M1 cases
are unchanged; the new mixed painter is a separate qualification gate.
