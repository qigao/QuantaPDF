# Quarantined independent W-only Clip originals (#262 / Draft #263)

**Not a new W* pixel positive, corpus admission, native pixel test, or SDK
backend transition.** Each file is an unchanged, independently published PDF
at an immutable revision. Exact bytes are checked before and after the real
selected-page Source -> Graph -> owned detached IR `observe` pipeline on
Linux Release/ASan+UBSan, macOS and Windows. Both `lowered` and clean
`unsupported` are acceptable observations; unsupported MUST leave zero IR.
The report MUST show PDFium/native raster `not_run`, no source provenance
approval and zero additional W* positives. Do not add these to the formal
six-original manifest before separate document-specific license, typed and
original-PDFium/native-C11 pixel admission.

| Upstream original (fixed revision) | Exact Git blob | Size | SHA-256 | Lexical operations | Intended triage |
|---|---|---:|---|---|---|
| [yWorks clippath-svg.pdf](https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/test/clippath/clippath-svg.pdf) | `2e6cd29a78da5d76bfad462be86ddbd463d3d4df` | 4107 | `7c9d3eab0f401537052a5ec4a8c531954aaf354061a9de25080376a9e13cb921` | W=5, W*=0, no observed BT/Do | Nested transforms/intersections, filled regions, strokes |
| [yWorks hidden-clippath.pdf](https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/test/clippath/hidden-clippath.pdf) | `525ca41a079a0ba7571faafc04091303ab226bb3` | 3309 | `6369893b1f5bd57790f098242d49775094d6c25a1a9b2316aaf1fd1f9a39bc71` | W=2, W*=0, no observed BT/Do | Clip scope and clipped paint |
| [jsPDF w3s_clip.pdf](https://github.com/parallax/jsPDF/blob/20d32998267745aa5336e299baa65d4f45916fb5/test/reference/w3s_clip.pdf) | `997ed4fbb327289740d7db1dc45344f1e1233fb9` | 3344 | `179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e` | W=1, W*=0, no observed BT/Do | Different PDF producer: jsPDF nonzero clip/fill geometry |

- yWorks upstream revision: `b564556f41b245893fbadd3a6c4f832455383dbe`;
  publisher repository [MIT license](https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/LICENSE);
  preserve adjacent `yworks-MIT-LICENSE.txt`.
- jsPDF upstream revision: `20d32998267745aa5336e299baa65d4f45916fb5`;
  its upstream [MIT license](https://github.com/parallax/jsPDF/blob/20d32998267745aa5336e299baa65d4f45916fb5/LICENSE)
  has copyright from James Hall and yWorks; copy retained in
  `jsPDF-MIT-LICENSE.txt`. The original reference PDF declares
  `/Producer (jsPDF 0.0.0)`; no contrary per-file notice observed.
- The published PDFs have **not** been re-exported, normalized, modified,
  filtered, or made from QuantaPDF-generated fixtures. Lexical counts are
  only candidate discovery, not a complete-page parser or pixel oracle.

Other unchanged upstream yWorks siblings found in this reconnaissance:
`clippath.pdf` (W=4, W*=0, BT present) and `clippath-empty.pdf`
(W=1, W*=0). They are *not* included in the test-only sample gate.
Separately inspected Mozilla PDF.js `test/pdfs/clippath.pdf` is also W-only
and requires further document-level license review; no bytes included.

**Existing exact-head official evidence remains 6 originals with external
W* native pixel-positive = 1/1**. The next positive must use a separately
licensed, byte-pinned original W* source and full PDFium original pixels
versus detached C11 comparison on exact-head three-platform CI.
