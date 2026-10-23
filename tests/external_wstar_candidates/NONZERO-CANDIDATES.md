# Quarantined yWorks W-only Clip candidate for Issue #266

This file is an unchanged upstream original, included only for candidate-level
qualification. It is **not** admitted to `tests/external_source_corpus/manifest.json`
and does not change formal source counts or any W* positive counter.

- Publisher: yWorks / svg2pdf.js
- Immutable revision: `b564556f41b245893fbadd3a6c4f832455383dbe`
- Original: [`test/clippath/clippath-svg.pdf`](https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/test/clippath/clippath-svg.pdf)
- Git blob: `2e6cd29a78da5d76bfad462be86ddbd463d3d4df`
- Size: 4107 bytes
- SHA-256: `7c9d3eab0f401537052a5ec4a8c531954aaf354061a9de25080376a9e13cb921`
- Repository license: MIT; retain `yworks-MIT-LICENSE.txt`. Formal document-specific admission review remains tracked by [Issue #271](https://github.com/qigao/QuantaPDF/issues/271).

The candidate has a 500×800 page, six painted Paths, five nonzero `W` Clip
leaves and four Clip intersections; it has zero `W*` leaves. The test requests
a single explicit path-lowering budget of eight painted paths, requires the
unchanged original to lower to fully owned IR, drops Source/Graph before the
private C11 raster, and compares the raster against the original PDFium C API
image. It checks seven interior color probes, whole-image RGB mean error
(`<=0.05`), zero gross-error pixels, repeat-RGBA identity, and at least 99%
red-ink overlap. It also tests malformed/orphan Clip prefixes and the six-op
capacity boundary still fail closed.

This is a **W-only candidate pixel result**, not a W* positive and not formal
corpus admission. Current master has five admitted originals; the formal
external W* pixel-positive count remains zero. No public ABI or production
backend cutover follows from this test.
