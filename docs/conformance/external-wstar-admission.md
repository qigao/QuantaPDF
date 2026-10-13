# Original-source W* Clip conformance: yWorks evidence and admission (#262)

## Provenance, permissions, and exact bytes

Publisher: [yWorks GmbH svg2pdf.js](https://github.com/yWorks/svg2pdf.js)
at immutable upstream revision
[`b564556f41b245893fbadd3a6c4f832455383dbe`](https://github.com/yWorks/svg2pdf.js/tree/b564556f41b245893fbadd3a6c4f832455383dbe).

- Original, **unchanged** PDF: [`test/clippath/clippath-cliprule.pdf`](https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/test/clippath/clippath-cliprule.pdf)
- SHA-256: `9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783`
- Upstream Git blob: `5b5b629da9faa1a969e8bf3290b4439dc01bc860`
- Original bytes: `3747`; selected page index: `0`
- Corresponding published SVG: [source](https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/test/clippath/clippath-cliprule.svg)
- License: [MIT](https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/LICENSE), copied verbatim into `tests/external_source_corpus/YWORKS-MIT-NOTICE.txt`. Copyright yWorks GmbH (2015–2026) and Vitaly Puzrin (2013–2015). See document-specific [review #265](https://github.com/qigao/QuantaPDF/issues/265).

The PDF in the official source corpus is the **identical Git blob** as the
quarantined discovery fixture. It is not regenerated or rewritten through
QuantaPDF, QPDF, Composer or PDFium.

## Real typed and native evidence

This original has three painted Path operations, two `W` nonzero Clip leaves,
two `W*` even-odd Clip leaves and three intersection nodes. All relevant
selected-page content and resource references are accepted by the bounded
Document → Graph Snapshot → detached owned IR pipeline, without Text,
XObjects, images, Forms, font resources or soft masks. The source Document and
Graph are released before native rasterization.

The original-page PDFium oracle is independent of Composer/IR export. The
private pure-C11 path processes the same detached IR on a **600 × 600** page,
including the newly bounded **open 1-device-pixel butt-cap stroke** under
nested Clip scopes. There are **no alternate or fallback render paths**.

Candidate-level exact-HEAD [CI #38341661819](https://github.com/qigao/QuantaPDF/actions/runs/38341661819)
passed Linux Release, Linux ASan+UBSan, macOS and Windows, with separately
original-PDFium/C11 fixed interior goldens, whole-image RGB budgets, gross
error checks, red stroke overlap and exact repeat RGBA identity. Observed
four-platform metrics:

| Measurement | Value |
| --- | ---: |
| Interior goldens | 6 |
| Mean whole-image RGB error | 0.006831 |
| Gross-error pixels | 0 |
| Reference/native red pixels | 302 / 302 |
| Overlapping red pixels | 302 |
| Repeated RGBA bytes | Identical |
| W / W* Clip Path leaves | 2 / 2 |
| Clip intersections | 3 |

This CI validated a **candidate-only** report with supported-count zero.
It is not the final six-source integrated CI result.

## Official admission boundary (not an SDK cutover)

The staged official manifest records an additional sixth source alongside the
previous five. A distinct CTest emits an **official original-source native
semantic report** *only after* the same original PDFium/C11 test succeeds.
The cross-report reconciler requires strict identity, SHA-256 admission,
document-level MIT-review link, selected page, correct source-based semantic
status, winding leaves, Clip intersections, fixed goldens, sample budgets,
repeat-byte identity, pixel bounds and genuine external counter `1`.
Missing, forged, candidate-only or stale evidence rejects rather than
implicitly trusting metadata.

The fixed generated 14/36 tests never enter the external denominator.
The old PDFium late-Text W* source remains strictly `UNSUPPORTED` with zero
partial IR; `W` and `W*` counters remain distinct.

**Do not increment official external W* positive evidence until the staged
six-document exact-HEAD Linux Release/ASan+UBSan, macOS, Windows and installed
SDK CI succeeds.** Keep [#262](https://github.com/qigao/QuantaPDF/issues/262)
open, [Draft / DO NOT MERGE PR #263](https://github.com/qigao/QuantaPDF/pull/263)
unmerged, and the public PDFium-backed SDK unchanged. The broader roadmap
[#208](https://github.com/qigao/QuantaPDF/issues/208) is not authorized to
switch rendering backends by this narrowly scoped W* evidence.
