# External original PDF candidate: yWorks svg2pdf.js even-odd Clip

**QUARANTINED: discovery source with independent typed and candidate-only pixel evidence; not the official six-source counter.**
Never count it in the five already admitted originals, the fixed generated
14/36 corpus or an external W* pixel-positive numerator on the basis of
this gate alone. Do not merge PR #263.

- Upstream: https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/test/clippath/clippath-cliprule.pdf
- Exact upstream revision: `b564556f41b245893fbadd3a6c4f832455383dbe`
- Raw PDF size: 3,747 bytes; PDF version 1.3
- Original Git blob: `5b5b629da9faa1a969e8bf3290b4439dc01bc860`
- Original raw SHA-256: `9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783`
- Publisher's MIT LICENSE: https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/LICENSE
- Copyright yWorks GmbH 2015-2026 and Vitaly Puzrin 2013-2015. Preserve
  `yworks-MIT-LICENSE.txt`. No contrary file-specific notice found in
  the upstream fixture.
- Corresponding SVG: https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/test/clippath/clippath-cliprule.svg
- Original content has two `W*` even-odd Clips and two ordinary `W` Clips
  with nested q/Q, opaque Path paint and stroke, with no observed
  BT Text or Do XObject operator. This lexical triage is not page admission.

The original `.pdf` is copied as raw bytes **unchanged**, not Composer-
exported, optimized, filtered or rewritten. CTest uses the existing test-only
`--preflight-original-pdf ... --expect-status observe` actual full selected-
page Source -> Graph Snapshot -> detached IR pipeline. The SHA-256/size
must match both before and after. If `lowered`, there must be actual even-odd
Clip Path leaves; if `unsupported`, there must be no partial IR/resources.
Both outcomes have `observation_only: true`, source provenance false, PDFium
pixel and native raster `not_run`, and external W* pixel-supported count `0`.

An admitted native W* **pixel positive** still requires independent original
PDFium-vs-owned-detached-C11 whole-image comparison, fixed interior goldens,
bounded gross/RGB errors, deterministic RGBA repeat, and exact-head three-
platform CI. Public SDK remains PDFium-backed.

## Candidate-only original PDFium versus detached pure-C11 pixel gate

`quantapdf.external_yworks_wstar_candidate_pixels` renders the **original**
unmodified 600x600 PDF through independent PDFium, then obtains the owned
detached IR through the actual Source -> Graph Snapshot pipeline, drops the
Source/Graph before C11 rasterization, and renders twice with bounded
operations, clip resources, segments, pixels and work.

The test checks separate `W` and `W*` path leaves, Clip intersections,
fixed interior blue/white semantic goldens drawn from the independently
published SVG's nonzero-vs-even-odd star geometries, whole-image RGB error
and gross-pixel budgets, line ink intersection, and byte-identical repeat
RGBA. A separate CMake wrapper hashes the original file before and after.

This is an **experimental candidate-only pixel gate**, not the formal
five-original reconciliation. Any successful report explicitly states
`candidate_pixel_parity_passed_not_admitted` with
`native_wstar_clip_supported_count: 0`; the independent pixel evidence
must pass exact-head Linux Release + Sanitizers, macOS and Windows and
be reconciled through a separate admission review before counters change.

## Separation from officially admitted source evidence

A byte-identical source is staged separately at
`tests/external_source_corpus/documents/yworks.clippath-cliprule-v1.pdf`,
with its own MIT license notice, manifest entry, official CTest pixel report
and strict cross-report reconciliation. See
[`docs/conformance/external-wstar-admission.md`](../../docs/conformance/external-wstar-admission.md).
This **candidate-only** directory and its `candidate_pixel_parity_passed_not_admitted`
report do not contribute to the admitted external positive count. The
new six-source CI must pass before any count of 1 can be claimed.
