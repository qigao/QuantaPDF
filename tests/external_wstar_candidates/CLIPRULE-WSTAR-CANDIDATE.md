# Quarantined yWorks original W* / even-odd Clip candidate for Issue #262

This is an unchanged upstream PDF from the yWorks/svg2pdf.js test corpus, pinned only for independent source-PDFium versus detached C11 Native pixel qualification. It is **not** in `tests/external_source_corpus/manifest.json`, and must not increment the formal external W* positive counter before the separate document-specific rights/admission gate (#271) is completed.

- Publisher: yWorks / svg2pdf.js
- Immutable upstream revision: `b564556f41b245893fbadd3a6c4f832455383dbe`
- Original SVG: [`test/clippath/clippath-cliprule.svg`](https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/test/clippath/clippath-cliprule.svg)
- Original PDF: [`test/clippath/clippath-cliprule.pdf`](https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/test/clippath/clippath-cliprule.pdf)
- Original Git blob: `5b5b629da9faa1a969e8bf3290b4439dc01bc860`
- Raw PDF bytes: 3747
- Raw SHA-256: `9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783`
- Repository-level license: MIT; retain `yworks-MIT-LICENSE.txt`. This is candidate-level evidence only; no formal redistribution/admission decision is implied.

The unchanged PDF has a 600×600 page with three painted operations: a red horizontal Stroke under an even-odd `W*` clip with a rectangular hole, a blue rectangle clipped by a self-intersecting nonzero star, and a blue rectangle clipped by the same star using `W*`. The oracle must assert at least one actual even-odd Clip leaf and verify the nonzero/even-odd difference at interior pixel probes, in addition to full-image PDFium parity and repeat-RGBA identity.

The candidate report must say `candidate_wstar_pixel_parity_passed_not_admitted`, `official_corpus_admitted: false`, and `external_wstar_pixel_supported_count: 0`. Do not add this document to the formal manifest in this change.