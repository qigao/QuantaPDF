# External W* original-source candidate screening (#262)

Status: **screening only; no new external positive**. Tracks
[#262](https://github.com/qigao/QuantaPDF/issues/262) after the original
`W n` stroke-pixel gate in #254/#261 passed. This document is *not* a
redistribution/license approval, a PDFium-vs-native pixel result, or an
extension of public SDK compatibility.

## Pinned upstream and screened surface

- Independent publisher: [PDFium](https://github.com/chromium/pdfium).
- Repository revision: `a84323421e94f484faca52dd9d027934eba42ab8`
  (the same immutable revision as the accepted `clip_path.pdf`).
- Scope: `testing/resources` at that revision; 194 `.in` files with
  published `.pdf` counterparts were checked for explicit standalone
  `W* n` tokens (one template could not be UTF-8 decoded), and 62
  additional published `.pdf` files lacking paired `.in` were inspected
  for literal, uncompressed `W*` tokens.
- The independently published `testing/resources/pixel` subdirectory was
  additionally screened at the same revision: 24 tracked original `.pdf`
  files were checked for literal raw `W*`; one text-bearing candidate was
  found. Two of those PDFs advertise `FlateDecode`, so this probe does not
  establish completeness for compressed streams.
- These are **candidate probes**, not a complete search: compressed PDF
  content streams, non-line-separated operators, nested directories and
  unparseable files can escape text scanning. A complete page-content
  parser/preflight is mandatory before admission. `.in` templates
  themselves are not external source PDFs.

## Exact original PDF candidates requiring rejection

The following are independently published **unchanged `.pdf`** blobs
with actual `W* n` observed in the original content stream. The immutable
Git blob identifies candidate bytes; a future admission still requires
raw-byte SHA-256 and document-level redistribution review.

| Original PDF (at revision above) | Git blob SHA-1 | Actual page content / why unsupported |
|---|---|---|
| `testing/resources/bug_1574.pdf` | `df274187155e8c02b86b1e51eb3347e29759fad4` | `q`, rectangular `W* n`, opaque `f*`, then nested `q` and `BT/Tj/ET` text. Cannot omit the text to claim an original-source positive. |
| `testing/resources/tagged_table.pdf` | `6e2bf9e4967b85939336da81e64df795ffd52037` | Two `W* n` scopes followed by marked content and `/Im7 Do`, `/Tr8 Do` XObjects. A fill-only or stroke-only native path cannot reproduce the original image. |
| `testing/resources/tagged_table_bad_parent.pdf` | `43d9aa38a4e4ec416d0cf4c22a868b1ffb0d198f` | Same XObject/marked-content blocker; not an independent positive primitive. |
| `testing/resources/bug_601362.pdf` | `d21a83405ea6469ec329ab31ad207362fcdd6706` | Repeated `W* n` scopes followed by `BT/TJ/ET` and graphics-state operations. |
| `testing/resources/text_font.pdf` | `78f3bcc784a08d0abf0948d78c52881fafca6d49` | `W* n` followed by font-dependent `BT/TJ/ET` text. |
| `testing/resources/pixel/bug_925736.pdf` | `429d53a477fc436692a00c08a2a7b4e867e1cce3` | Original uncompressed `W* n`, followed by `BT`, font-dependent `TJ`, and `ET` text. It remains outside the current Path-only subset. |

Only the precisely pinned, document-license-reviewed
`bug_1574.pdf` is admitted to the external corpus under
[#264](https://github.com/qigao/QuantaPDF/issues/264), exclusively as
an **UNSUPPORTED late-Text negative**. All other sources in this table
remain unadmitted. Never strip unsupported operators, infer
redistribution rights, or increment a native W* pixel-positive counter
merely because a token `W*` appears. Generated and external evidence
remain separate populations.

## Independent fpdf2 published PDF screening (compressed streams decoded)

- Upstream `py-pdf/fpdf2` immutable revision:
  `a3cca5cc77144775603ace8467c0743e2deee53f`.
- Published `test/svg/generated_pdf/clip_path.pdf` (Git blob
  `be2d337add45e0b110c56c1326c8d011c8cb6206`): decoded source
  stream contains `W n` with a green filled curve, **not W***.
- Published `test/drawing/generated_pdf/bounding_box_painted_path_evenodd_fill_with_hole.pdf`
  (Git blob `3161ce2a62ca94a6fb87a4fb7849eee0e8f4737b`):
  decoded operators contain **`f*` even-odd paint**, but no `W*` Clip.
- Published `test/svg/generated_pdf/path_clippingpath.pdf`
  (Git blob `1871beb4bd24f1a66ca9b598ba601f76cf6a1eb2`):
  decoded long source content stream contains no `W*` operation.

The blobs above were screened as **originals**, not rewritten, and
their byte identities refer to Git blobs (not a substitute for SHA-256
or document-specific license review). This is a limited selection, not
a claim of complete fpdf2 corpus coverage. Neither `f*` nor
nonzero `W` can satisfy #262's W* positive counter.

## Offline W* source-candidate scanner (screening only)

Run the portable, bounded, standard-library-only diagnostic on existing,
**unchanged** original PDFs:

```sh
python3 tools/qualification/screen_external_wstar.py \
  --pdf /path/to/original.pdf --report /tmp/wstar-screen.json

# For a separately downloaded, unchanged local corpus:
python3 tools/qualification/screen_external_wstar.py \
  --pdf-dir /path/to/pinned-upstream-pdfs --report /tmp/wstar-corpus-screen.json
```

The scanner records an original-byte SHA-256, examines raw, Flate-decoded,
strictly bounded ASCII85, ASCIIHex, and ordered ASCII85/ASCIIHex-to-Flate
streams using direct /Length or uniquely resolved simple bounded integer
/Length references with precise endstream boundaries (at most 128 PDFs, 16 MiB per source, 64 streams per source and
1 MiB decoded per stream). Directory discovery is bounded to 4,096 entries
and 16 levels, sorted deterministically, never follows symlinks, and
rejects duplicate basenames. It is offline and never downloads files. A PDF header is accepted at
offset 0..1023 (bounded leading printer bytes are ignored during scanning),
and the JSON records `pdf_header_offset`. Candidate object-header lookup
never crosses into the leading non-PDF preface; a spoofed preamble
indirect object cannot authorize a stream in the PDF body. This is not
full PDF page parsing.
On a validation failure, an existing report remains untouched; output paths
that hardlink an original input are rejected before any write.
The scanner distinguishes `W*` from
`W` and `f*`. It flags later Text/XObject/Image/marked-content
operators, truncated/unknown streams, and files needing complete page
preflight. It never edits, generates, fetches, or installs a candidate.

**Heuristic only:** stream recovery is not full PDF object-resolution;
compressed object streams, missing/duplicate/complex indirect
/Length references, non-identity PNG/TIFF /DecodeParms predictors, unresolved DecodeParms references, inherited
resources and unapproved filters/filter orders remain inconclusive.
The narrowly accepted additional filters are a single PDF ASCII85Decode
or ASCIIHexDecode stream, plus either ASCII85Decode -> FlateDecode or
ASCIIHexDecode -> FlateDecode in that order, without DecodeParms. PDF
ASCIIHex requires the '>' terminator, enforces valid hex digits, permits
PDF whitespace and pads a final unpaired nibble with zero. Invalid hex,
malformed terminators, reversed/unknown filter orders, decode expansion
over budget and ambiguous filter parameters remain inconclusive. No source PDF is rewritten. Missing or mismatched stream
boundaries now fail closed; inner /DecodeParms dictionaries do not hide
outer /Filter /FlateDecode. Embedded literal 'endstream' does not
truncate a source stream. This is still **not** complete object/page
resolution. Even a `candidate_requires_full_page_preflight`
result is **not** a native-support claim: verify the complete selected
page, source/license, immutable revision, independent PDFium pixels and
cross-platform CI separately. The JSON explicitly fixes the W* native
positive count to zero and marks license/page attribution unreviewed.
55 synthetic + 3 pinned original-PDF scanner regression cases run as part of the existing
Linux, macOS and Windows workflows; no new CI workflow is introduced.

The exact, previously reviewed unchanged PDFium `clip_path.pdf` and
`bug_1574.pdf` are now included in **three real-source scanner regressions**:
W is not W*, W* with late BT/Tf/Tj/ET is not a positive, and a combined
screening report preserves both immutable byte hashes while reporting zero
native W* passes. Their test expectations are strict but do not replace
real native source/PDFium pixel conformance.

## Complete selected-page Typed IR preflight (test-only)

The offline Python token scanner is intentionally **heuristic**. The new
`--preflight-original-pdf` mode in the **existing test-only**
`quantapdf_test_native_source_qq_oracle` binary instead runs the real
bounded **Source Document → Graph Snapshot → Detached Composer IR**
selected-page pipeline, including every relevant admitted page content
stream, inherited page geometry, q/Q scope, Clip and resource preflight.
Document/Graph ownership is released before inspecting detached IR.

```sh
quantapdf_test_native_source_qq_oracle \
  --preflight-original-pdf /path/to/unchanged-original.pdf \
  --page-index 0 --expect-status unsupported \
  --preflight-report /tmp/selected-page-status.json
```

Valid expectations are exactly `lowered` and `unsupported`. A rejected
original MUST leave zero Pages, Operations, Clip resources, Graphics
States, fonts, images, Forms and masks. A lowered original must remain
within the documented narrow resource and operation limits. Path
canonicalization and symlink/hardlink checks protect the original input
from accidental report/staged-file overwrites.

This mode does **not** validate a document's license, provenance or hash,
does **not** render any pixel, and does **not** call the PDFium oracle.
The deterministic JSON reports `original_source_provenance_verified:false`,
`native_raster:not_run`, `original_pdfium_pixel_oracle:not_run` and
`external_wstar_pixel_supported_count:0` even for a typed `lowered` page.
Source-specific provenance and SHA-256 remain obligations of the external
corpus manifest, independent from a parser-success status.

Two dedicated CTests run against unchanged pinned originals:
`quantapdf.external_original_w_typed_preflight` (typed `lowered` for
`clip_path.pdf`) and
`quantapdf.external_original_wstar_text_typed_preflight`
(`UNSUPPORTED` + empty IR for `bug_1574.pdf`). These use the same three
existing CI platform jobs; each uploads its report alongside the separate
W-positive or W*-negative external semantic artifact. They are not added
to the fixed 14/36 generated corpus, and neither increases the external
W* **pixel-positive** count.

## Typed Clip-path winding evidence (#262)

The native source-PDF oracle now counts **actual owned detached IR
Clip Path leaves** by their winding rules, separately from DAG
\`CLIP_INTERSECTION\` nodes. For every generated original-PDFium/native
nested-Clip pixel oracle with admitted nonempty Clip graphs, it fails if
a source containing \`W* n\` has no even-odd Clip leaf, or if a W-only
source unexpectedly acquires an even-odd leaf. This prevents a false W*
acceptance caused by collapsing \`W*\` into nonzero \`W\`, even when
the two geometries happen to produce identical pixels.

The selected-page typed preflight JSON includes distinct
\`detached_ir_w_nonzero_clip_path_leaves\`,
\`detached_ir_wstar_evenodd_clip_path_leaves\` and
\`detached_ir_clip_intersection_nodes\`. On a late unsupported source,
the entire IR and all three counts remain zero by contract; this is
**not** proof that the original source lacked a W* operator.
Token detection, complete typed admission and independent **original**
PDFium vs native C11 pixel rendering remain three separate evidence
steps. The external W* pixel-native-positive count remains zero.

## Native preparation in this branch

`quantapdf.native_scoped_clip_dag_v1` now exercises a provider-free C11
three-operation scenario in which a nested even-odd **two-contour hole**
suppresses red paint, later blue paint is scoped to the restored outer
Clip, and green is unrestricted. The same two contours must differ
visibly when changing `W*` to nonzero `W`. Repeat RGBA identity and
invalid rule atomic rejection are checked. This is *not* evidence that an
independently published W* PDF passes the source Graph/IR path or PDFium
pixel oracle.

## Next acceptance gate

1. Find a byte-pinned, separately license-reviewed, published **original
   PDF** with true `W*` (ideally nested `q/Q`) and only the currently
   supported opaque Path operations, or scope a separate primitive
   expansion first. Preserve complete original source bytes.
2. Preflight every operator and resource on the selected page, including
   graphics-state changes and late operations; fail closed with no
   partial detached IR for text, images or XObjects.
3. Independently render **the original source file** with PDFium, not a
   Composer export; verify source Document/Graph are disposed before
   native render, fixed interior goldens, whole-image RGB/gross-error
   thresholds and deterministic repeated RGBA bytes.
4. Publish a distinct reconciled external `W*` positive count only with
   exact-head Linux Release+ASan/UBSan, macOS, Windows and installed SDK
   evidence. No public ABI changes, no PDFium fallback or SDK cutover
   (roadmap #208).

The generated `operators.unsupported_text` source oracle now tests
**late `BT/ET`** after the exact previously qualified three painted PATH
operations under nested W/W* scopes. If the final unsupported text token
appears, the entire detached IR must remain empty (pages, operations,
Clip and graphics-state resources). It replaces the prior early-text
fixture within the same registry slot and does **not** increase 14/36.

The exact original `bug_1574.pdf` has now been admitted solely
for original-PDFium/nonblank + native-UNSUPPORTED late-text regression
([#264](https://github.com/qigao/QuantaPDF/issues/264)). It has
not passed W* native pixel parity and adds zero supported W* sources.

Current external W* positive count: **0**. Reconciled external semantic
JSON now distinguishes `native_w_clip_supported_count: 1` from
`native_wstar_clip_supported_count: 0`; attempts to report a positive W*
count without a qualified original source must fail closed.

## Additional immutable upstream search checkpoint (2026-10-13)

- [Mozilla PDF.js at `68f5e2182525acc90c030fd839f6cda90d555c17`](https://github.com/mozilla/pdf.js/tree/68f5e2182525acc90c030fd839f6cda90d555c17/test/pdfs), published `test/pdfs` Git tree `17d31edb817fbc716cfd9558a7f203b364c0a9b8`: screened **raw unchanged bytes** of 180 small `.pdf` files. Reproduce selection by filtering the tree to regular `.pdf` blobs with size 200..2,600 bytes, sorting by byte size then Git tree filename order, and selecting the first 180. No literal whitespace-delimited `W*` was observed in their raw source bytes; `clippath.pdf` uses `W`, not `W*`. This was **not** decompressed selected-page screening: Flate/ASCII encodings, inherited content/resources and remaining PDFs are not excluded. No PDF.js file was admitted or relicensed.
- Previously reviewed fpdf2/PDFium and PDF Association candidates remain screening-only or strict unsupported negatives unless individually approved. Upstream repository code licenses alone do not establish document-level redistribution rights.

**No native original-source W* pixel-positive has been found.** The next gate remains a complete selected-page typed-original `W*` Path/Clip admission followed by unchanged-source PDFium versus owned Detached IR/C11 pixel goldens, full-image budgets and deterministic repeat on exact-head CI. Keep [#262](https://github.com/qigao/QuantaPDF/issues/262) OPEN and Draft [#263](https://github.com/qigao/QuantaPDF/pull/263) unmerged.

## Additional independent PDF 2.0 source review (2026-10-13)

- Publisher: [PDF Association PDF 2.0 examples](https://github.com/pdf-association/pdf20examples), immutable repository tree `c20f2c17bfcc4baab7cfe62e70fae64caf14d5fa`.
- The unchanged upstream `PDF 2.0 with offset start.pdf` (5,264 bytes, Git blob `4cf62cc204ecbc2c2027451ea265ab8978510c6b`) contains printer-preface text and begins the actual `%PDF-` header at **byte 656**. See [the publisher's source](https://github.com/pdf-association/pdf20examples/blob/c20f2c17bfcc4baab7cfe62e70fae64caf14d5fa/PDF%202.0%20with%20offset%20start.pdf). The repository's [LICENSE.md](https://github.com/pdf-association/pdf20examples/blob/c20f2c17bfcc4baab7cfe62e70fae64caf14d5fa/LICENSE.md) grants PDF files CC BY-SA 4.0; this is *not* the CC BY 4.0 corpus from `pdf-differences`.
- Seven upstream PDF 2.0 examples were checked for literal, whitespace-delimited raw `W*` tokens; **none was observed** in that raw probe. This does not exclude compressed/structural streams or constitute selected-page semantic preflight. No new original asset has been admitted, redistributed or modified, no raw-byte SHA-256 conformance pin has been recorded, and no W* external-positive counter changes.
- Five synthetic header-framing regressions exercise offset 656, a spoofed preamble stream, the 1023/1024 boundary, and malformed version rejection. The scanner reports the header offset but is still **screening-only**. Only real selected-page Source→Graph→Detached IR admission and original PDFium/C11 pixels may qualify an external W* positive.
