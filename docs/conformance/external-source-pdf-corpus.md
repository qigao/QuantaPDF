# External PDF conformance corpus: admission contract (v1)

Tracks [#233](https://github.com/qigao/QuantaPDF/issues/233),
[#229](https://github.com/qigao/QuantaPDF/issues/229), and the
PDFium-free product exit gate [#208](https://github.com/qigao/QuantaPDF/issues/208).

## First candidate external document: independently published CC0 PDF

The first v1 baseline had **zero** externally sourced PDFs. Issue
[#250](https://github.com/qigao/QuantaPDF/issues/250) now records the
publisher's document-set CC0 dedication and exact source identity for
`tinytoolkit.sample-1-page-v1.pdf`. Its inclusion is limited to **one
external status-only source**; the generated **14 families / 36 QPDF-created
test executions** remain entirely separate.

| Property | Pin |
|---|---|
| Source | [tinytoolkit-org/pdf-sample-files/out/sample-1-page.pdf](https://github.com/tinytoolkit-org/pdf-sample-files/blob/4fd733c47ead5446b06372ab910cb78d1c7da55e/out/sample-1-page.pdf) |
| Revision | `4fd733c47ead5446b06372ab910cb78d1c7da55e` |
| Source license | [Publisher's CC0 sample-file declaration](https://github.com/tinytoolkit-org/pdf-sample-files/blob/4fd733c47ead5446b06372ab910cb78d1c7da55e/README.md) and CC0 LICENSE |
| SHA-256 | `ada1603d4396b84ac0d85ca940b0149df13fd4a6beb46f2f2f62c4c8a6bc69b2` |
| Bytes | 3,312 |
| Page | 0 |
| Expected native outcome | `UNSUPPORTED` (PDF text/fonts are outside current Path/Clip subset) |

The exact source Git Blob SHA-1 is
`ddab89a772c7727cfab3d50f094a41a6950292cd`, matching the vendored
blob in this repository. The PDF is used **as-is** without QPDF rewriting
or synthetic reconstruction. The publisher explicitly identifies the
sample PDFs as CC0 and machine-generated without personal data. This
is a per-source documented license declaration, not permission inferred
from the repository's code alone.

The distinct `quantapdf.external_cc0_text_source` CTest (reusing the
existing test-only executable) verifies the *original* file renders visible
pixels through test-only PDFium, then confirms strict Graph→detached IR
`QUANTAPDF_ERROR_UNSUPPORTED` with no partial page, operation, Clip or
graphics-state resources. No production renderer fallback exists. Its
atomic `external-cc0-text-status.json` includes
`status_conformance_passed_count: 1` **only when the test passes**, while
`native_supported_passed_count: 0` always. CI uploads the separate semantic
report from Linux Release/ASan+UBSan, macOS and Windows. This is a successful
**unsupported-operation rejection** test, not a successful native render.

This semantic outcome does **not** alter the admission tool's own
`native_conformance_passed_count: 0`: admission checks provenance and
binary integrity only. External supported pixel parity remains unqualified.

An external PDF must be a pre-existing document with an independently
recorded source and its own copyright/licensing status. A repository's
open-source *code* license does not necessarily license every attached PDF.
No files are downloaded during CI.

## Second external document: CC0 blank Letter native-positive baseline

[#252](https://github.com/qigao/QuantaPDF/issues/252) pins the exact
independently published `sample-blank-letter.pdf` from the same upstream
CC0 sample-file set, revision `4fd733c47ead5446b06372ab910cb78d1c7da55e`.
The source is 1,370 bytes with SHA-256
`a53d48827af9f28e3616195bae3d345d5b2708100d0ec43f222f4a19f785cf5c`
and matching upstream/vendored Git Blob
`12b76d33fa368d2e2a8234448c44ab85e42ecf04`.

This fixture has one **612×792 Letter page and no /Contents**.
`quantapdf.external_cc0_blank_native` parses the original PDF via
Graph → detached IR and requires exactly one blank page with zero path,
text, image and Clip resources. The existing private pure-C11
`quantapdf_ir_raster_render_v0` consumes that detached IR without calling
PDFium. The original source file is rendered separately using test-only
PDFium, and every RGB channel of every pixel is compared against native
RGBA composited over white. Repeat native raster bytes must match exactly.

The standalone semantic report records
`native_blank_page_supported_count: 1`,
`native_path_clip_supported_count: 0`, and
`native_text_supported_count: 0`. The admission report remains
`integrity_verified_only` and contains no renderer-pass claims.
**This is a positive external blank-page qualification, not a Path/Clip
compatibility success and not complete PDF compatibility.** The required
future first external painted/Clip path pixel-oracle remains under #233.

The repository includes two externally sourced CC0 PDFs:
one strict text `UNSUPPORTED` example and one blank-page positive baseline,
plus one separately reviewed PDFium opaque-PATH test asset.
The generated 14 families / 36 PDF executions are unaffected, and no
external document is generated or downloaded in CI.

## Third external document: original PDFium opaque black PATH

Source and publisher license evidence: [#256](https://github.com/qigao/QuantaPDF/issues/256).
The exact *original PDF* is
[PDFium testing/resources/black.pdf](https://github.com/chromium/pdfium/blob/a84323421e94f484faca52dd9d027934eba42ab8/testing/resources/black.pdf),
pinned to commit `a84323421e94f484faca52dd9d027934eba42ab8`
(923 bytes, SHA-256
`41974886a825bb43056bc121a96780577b4f537c7776e705e485c55a1230bf4d`).
The upstream and vendored Git Blob IDs both equal
`8513d9f3fd64b1481bf3bd9cf8c963b6c6643b4d`.
Its publisher is PDFium Authors with a BSD-3-Clause license declaration
in the pinned repository `LICENSE`; the repository preserves that BSD
notice under `tests/external_source_corpus/PDFIUM-BSD-NOTICE.txt`.
The creator and producer fields identify Cairo 1.13.1 as the PDF authoring
software, not an independent copyright grant. The minimal test source
contains no external fonts, images, or personal information. This review
is specific to the **single reviewed asset**, not blanket approval for
other PDFium test fixtures.

The source has one 612×792 page. Its compressed original content stream
uses an opaque solid black rectangle with ordinary graphics-state save,
normal alpha `/a0 gs`, rectangle fill `re f`, and restore. The page
also advertises a DeviceRGB transparency group. The native acceptance
gate is deliberately limited to that exact **opaque** case; it is
*not* a general transparency-group, soft-mask, or blend-mode feature.

The separate `quantapdf.external_pdfium_black_path` CTest independently
renders original PDFium RGB and lowers the original document into detached
IR, disposing source document and Graph Snapshot before pure C11 raster.
The native raster now permits one bounded opaque operation; the test
requires a single PATH, no Clip nodes, no font/image/mask/form resources,
exact dimensions, repeat byte identity, black native golden pixels,
mean RGB difference <=3 and zero gross (>64) error pixels.
Report `external-pdfium-black-path-status.json` contains actual measured
per-page metrics, with `native_path_clip_supported_count: 1` and
`native_clipped_path_supported_count: 0` **only after the exact source
passes**.

The independent reconciliation gate compares this third semantic
report with the offline SHA-256 admission record and with prior text
rejection and blank positive reports. The three distinct external source
documents, 14 generated test families, and 36 generated source-PDF
executions are never merged into one compatibility percentage.

**Remaining**: one independently sourced PDF with actual W/W* Clip and
paint operation(s), original-source PDFium pixel parity and bounded
clip semantics. This remains [#254](https://github.com/qigao/QuantaPDF/issues/254).

## Admission ledger and offline tool

`tools/qualification/external_pdf_corpus.py` reads the source-controlled
manifest as untrusted input. It performs bounded, offline validation:
- Exact root/document schema (unknown fields and duplicate JSON keys fail).
- Stable unique IDs, HTTPS source and license URLs, immutable source revision
  metadata, explicit attribution and a linked QuantaPDF issue/PR for human
  license review.
- Limited initial SPDX allowlist: CC0-1.0, CC-BY-4.0, Apache-2.0,
  BSD-3-Clause, MIT. This is an **allowlist for manual review**, not proof
  that a document actually has such a license.
- File size <= 16 MiB, at most 64 documents, selected page index <= 255,
  bounded feature tags and a strict lowercase SHA-256.
- A locally supplied `tests/external_source_corpus/documents/<id>.pdf`
  file with exact byte size and matching digest, basic PDF header/EOF
  checks, no symlinks, no unregistered PDF files. These bytes are never
  fetched automatically by the validation script.

The CLI produces an atomic, deterministic JSON report. With an empty manifest, the result is `no_external_documents` and
`document_count: 0`. With checked-in, byte-verified source PDFs, the result
is `integrity_verified_only`, with its `document_count` representing
admitted **source bytes** only. It is **not a native compatibility pass**. The per-document expected native outcome is declarative, not
executed by this admission tool. The binary SHA-256 measures *exact source
bytes*, not canonicalized PDF semantic equivalence.

The tool never substitutes PDFium or qpdf rendering for native results,
and cannot change QuantaPDF's public SDK dependency graph.

## Procedure to admit subsequent licensed external PDFs

1. Open an issue/PR naming the document's owner, original URL, revision or
   durable snapshot, document-level license and attribution. Obtain a human
   review; do **not** infer rights from the host repository's LICENSE.
2. Retrieve the exact source document through an authorized method and
   record its SHA-256, byte size, page index and feature tags. Pin or
   preserve its source version. Check that the PDF content is not
   proprietary or personal; do not commit an unauthorized document.
3. If redistribution is permitted, review the file and add it as
   `tests/external_source_corpus/documents/<id>.pdf`, with a corresponding
   complete `manifest.json` entry. The manifest ID is a safe basename,
   not a relative path. If redistribution is prohibited, do not add that
   document to this checked-in, offline CI corpus.
4. Run `python3 -m unittest discover -s tests -p test_external_pdf_corpus.py`
   and then:

   ```sh
   python3 tools/qualification/external_pdf_corpus.py \
      --manifest tests/external_source_corpus/manifest.json \
      --assets tests/external_source_corpus/documents \
      --report build/external-corpus-admission.json
   ```

5. Require the same commands to pass on Linux, macOS and Windows CI. The
   existing QuantaPDF workflow uploads the admission JSON for each platform
   under `external-corpus-admission-*`. It does **not** create another
   workflow, download remote PDFs or install a new runtime dependency for
   the shipping native SDK.
6. After approval, add a distinct **semantic** test that opens the source
   and exercises Graph → detached IR → native rendering or an explicit
   fail-closed result, independently comparing original-source PDFium
   pixels for admitted supported cases. Only such independent evidence
   may raise the external **native conformance** counter above zero.

## Boundaries

The small Python test suite uses *synthetic PDF-looking bytes* solely to
exercise hashing, missing files, duplicates, unauthorized licenses, path
restrictions and atomic reporting. It does not constitute an admitted
external PDF, demonstrate PDF parser correctness or affect the generated
36-case pixel oracle.

Full native text, fonts, images, transparency, form/annotation semantics,
broader page geometry and public SDK removal of PDFium remain #208 exit
criteria. Admission/byte-integrity checks are intentionally separate
from both the existing generated corpus and the future external
render-compatibility qualification.

## Cross-report conformance reconciliation

The existing three-platform CI additionally invokes
`tools/qualification/reconcile_external_corpus.py` **after** the external
source status tests and the positive W* pixel test. This gate verifies the separate offline admission report
against actual original-PDFium/native semantic JSON for the exact six
pinned source IDs. A manifest's expected capability, PDFium's successful rendering
alone, or merely having a JSON file can **never** increment a native
Path/Clip pass counter. It fails on missing/swapped IDs, altered byte
admission status, missing test success, nonzero blank pixel error,
unjustified painted Path/Clip counts, or leakage from the 14/36
QPDF-generated fixture totals.

The published artifact `external-corpus-reconciled-{linux,macos,windows}`
That two-document count was the original historical baseline before the later
Path/Clip admissions. The current formal corpus and strict reconciliation are
summarized in the Issue #262 section below.

## Selected page transparency Group admission (#258)

The restricted native source-PDF lowering explicitly checks the selected
leaf /Page's /Group property before parsing content or publishing Composer
IR. An absent or explicit null group is accepted (PDF null is equivalent to
omission). A present dictionary must specify
/S /Transparency, /I true, /CS /DeviceRGB; /K false and /Type /Group
are optional. This narrowly bounded, isolated non-knockout DeviceRGB
configuration is permitted only with the C11 raster's existing opaque
Normal-alpha/resource preflight, not a general transparency compositor.

Different group subtypes, non-isolated /I false, knockout /K true, non-RGB
spaces, omitted required fields and unrecognized group keys return
UNSUPPORTED. Invalid value types return FORMAT. Both reject before any
detached Composer IR is published, with no PDFium production fallback.
/Group is a leaf page property, not an inheritable /Pages attribute; an
unsupported sibling group does not contaminate the selected page.

The existing original black.pdf remains qualified only for the exact
isolated DeviceRGB opaque scenario, using independent original-source
PDFium whole-page pixel comparison. The separate
quantapdf.native_page_group_admission CTest generates real positive and
negative PDF fixtures, verifies direct/indirect group resources,
deterministic native repeat output, successful pixel oracle, fail-closed
empty IR and page isolation. They do not add to the 14/36 generated
corpus baseline or the independently sourced external PDF ledger.

General transparent compositing, knockout, non-isolated, soft-mask,
backdrop and arbitrary color-space semantics remain unimplemented.
The installed public SDK remains PDFium-backed.


## Fourth external document: original PDFium W Clip and blue stroke

The publisher's original [clip_path.pdf](https://github.com/chromium/pdfium/blob/a84323421e94f484faca52dd9d027934eba42ab8/testing/resources/clip_path.pdf)
is independently authored by PDFium, not generated or rewritten by QuantaPDF.
The pinned upstream revision is
`a84323421e94f484faca52dd9d027934eba42ab8`.
Its exact raw length is **514 bytes**, SHA-256
`6674dbd092ef8ae55b6ab1cda6a8b966019fbf22f7556291253853303072288b`,
and matching upstream/vendored Git blob SHA-1
`8d27bcb6395965ae9c6d7fbd91e802d786a84ab7`.
Document-specific source and BSD-3-Clause attribution review are recorded
in [#260](https://github.com/qigao/QuantaPDF/issues/260);
the upstream license notice is retained under
`tests/external_source_corpus/PDFIUM-BSD-NOTICE.txt`.

The original 100×50 page's PDF content uses a rectangle
`10 15 m 40 15 l 40 35 l 10 35 l W n` as a nonzero-winding
Clip, then blue `0 0 1 RG` and a closed triangular
`10 10 m 25 40 l 40 10 l s` stroked path. It has **no text,
images, forms or soft masks**. It is a deliberate narrow expansion
from fill-only to one **opaque width-1 solid miter-joined closed triangle
stroke** under an isometric transform, **only when the three miter joins lie
outside the single rectangular Clip**. Otherwise the native engine
rejects before output allocation rather than approximating joins. It
does **not** imply arbitrary stroke width, cap/join, dash, transform,
unclipped strokes, or transparency support.

The original-source PDFium oracle is independent of Source → Graph →
detached IR → private C11 Native Raster, and original Document/Graph
ownership is released before the native render. The real-source
`quantapdf.external_pdfium_clipped_stroke` CTest compares entire-page
RGB pixels, requires visible blue stroke pixels and overlap with
the PDFium oracle, exact repeat RGBA bytes, and fail-closed rejection of
unsupported stroke configurations. An additional provider-free C11
test verifies owned single-stroke plus borrowed Clip intersection
without introducing PDFium/qpdf to the renderer itself.

Before Issue #264, the external ledger distinguished four original
source documents: one text-rejected, one blank positive, one opaque
single-PATH-fill positive, and this source's **clipped width-1 stroke**.
Issue #264 adds a fifth source solely as a late-text/W* rejection test;
it does not add a Native-positive capability. Successful **clipped Path**
conformance remains counted separately from opaque-fill, and the 14/36
generated corpus totals remain unchanged. No production PDFium removal or
public ABI change follows from these source-specific tests.

## PDFium W* late-text rejection — Issue #264

The fifth formal external original is the unchanged upstream PDFium
`testing/resources/bug_1574.pdf`, pinned at revision
`a84323421e94f484faca52dd9d027934eba42ab8`.

| Property | Pinned evidence |
|---|---|
| Source | [PDFium bug_1574.pdf](https://github.com/chromium/pdfium/blob/a84323421e94f484faca52dd9d027934eba42ab8/testing/resources/bug_1574.pdf) |
| Source template | [bug_1574.in](https://github.com/chromium/pdfium/blob/a84323421e94f484faca52dd9d027934eba42ab8/testing/resources/bug_1574.in) |
| Git blob SHA-1 | `df274187155e8c02b86b1e51eb3347e29759fad4` |
| Raw PDF SHA-256 | `900d0ff1911736c372e0c7ead2c68be338479b985c676572db1cc6e35483b890` |
| Size | 724 bytes |
| License | [PDFium project BSD-3-Clause license](https://github.com/chromium/pdfium/blob/a84323421e94f484faca52dd9d027934eba42ab8/LICENSE); notice retained in `tests/external_source_corpus/PDFIUM-BSD-NOTICE.txt` |
| Expected Native outcome | `UNSUPPORTED` — status-only negative evidence |

The original page is 200×300 points. Its content first defines a rectangular
even-odd clip with `W*`, paints a white path, and only afterwards executes
`BT`, `Tf`, and `Tj` for visible Times-Roman text. The early Clip/fill
must not allow the later text operators to be skipped or treated as a
successful partial page.

`quantapdf.external_pdfium_wstar_late_text` renders the unchanged original
directly through the public PDFium C API and requires visible non-white pixels
from the text that follows the earlier `W*` clip and `f*` fill. It then lowers
the original Document→Graph Snapshot→Composer IR path and requires
`QUANTAPDF_ERROR_UNSUPPORTED` with zero pages, operations, Clip resources and
graphics-state resources. It verifies the 724-byte source is unchanged before
and after both observations. The full source is the negative fixture; this
test does not claim the preceding W*/fill sequence as a separately qualified
Native-positive case. The semantic report is separately reconciled on Linux
Release, macOS and Windows; it increments only the
`external_wstar_late_text_rejection_passed` status counter and explicitly
requires `native_wstar_clip_supported_count: 0`. It never increments a Native
render-support counter. This is not evidence of positive W* pixel parity.

## Current formal original W* positive — Issue #262 (2026-10-23)

The unchanged yWorks original `test/clippath/clippath-cliprule.pdf` was
admitted as `yworks.clippath-cliprule-v1` following the source/rights review
recorded in [#271](https://github.com/qigao/QuantaPDF/issues/271).
It is now the sixth admitted original and first official W* pixel-positive
on master, separately from the proposed TeXample seventh source.
The original bytes are checked in without rewrite at
`tests/external_source_corpus/documents/yworks.clippath-cliprule-v1.pdf`;
the retained MIT notice is `tests/external_source_corpus/YWORKS-MIT-NOTICE.txt`.

| Property | Pinned value |
|---|---|
| Upstream source | [yWorks/svg2pdf.js original PDF](https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/test/clippath/clippath-cliprule.pdf) |
| Immutable revision | `b564556f41b245893fbadd3a6c4f832455383dbe` |
| Original Git blob | `5b5b629da9faa1a969e8bf3290b4439dc01bc860` |
| Size / SHA-256 | 3,747 bytes / `9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783` |
| License / review | MIT; [#271 source and redistribution review](https://github.com/qigao/QuantaPDF/issues/271) |
| Selected page | 0; 600×600 pixels |
| Native outcome | `supported-wstar-clip` |

The official `quantapdf.external_pdfium_wstar_pixel_positive` CTest compares
direct PDFium pixels from the unchanged source against pure C11 Native Raster
pixels produced from owned detached IR after Source and Graph are released.
It requires eight fixed interior goldens, the W-vs-W* star-center distinction,
the rectangular even-odd hole, full-image RGB/gross-error budgets, red/blue
overlap, repeat RGBA identity, source SHA preservation, and bounded resources.
The exact report is separately checked against the formal manifest by
`reconcile_external_corpus.py`; candidate-only reports are not accepted as
official evidence.

At the qualifying exact head, mean RGB error was 0.006831, gross-error pixels
were 0, red reference/native/overlap was 540/540/540, blue was 4770/4770/4770,
and repeated RGBA was byte-identical. Its completed admission increased
the formal denominator from five to six original PDFs and the official W*
pixel-positive numerator from zero to one. The existing W* late-text
source remains a separate unsupported-operation rejection; it is not
counted as a positive.
Generated 14 families / 36 executions remain separate from all six externally
sourced originals.

This single W* original is from yWorks and therefore does not by itself
establish cross-producer W* conformance. Broader nested-Clip, other-producer,
unsupported-operator, runtime and PDFium SDK cutover gates remain open under
[#262](https://github.com/qigao/QuantaPDF/issues/262),
[#214](https://github.com/qigao/QuantaPDF/issues/214),
[#233](https://github.com/qigao/QuantaPDF/issues/233) and
[#208](https://github.com/qigao/QuantaPDF/issues/208).

## Independent-producer second W* source — Issue #262 (2026-10-25)

The yWorks original above is the existing **sixth** admitted original and the
first official W* pixel-positive. The candidate source in
[PR #415](https://github.com/qigao/QuantaPDF/pull/415) is a distinct producer:
[TeXample's "Intersecting rings"](https://texample.net/intersecting-rings/),
attributed to **Dominique Wuertz**. The original TeXample page makes both
the PDF file and CC BY-SA 4.0 license statement available. The unchanged
source has a genuine parity-discriminating nonrectangular `W*`, orange
even-odd fill and two cubic outline strokes, without text, image or Form
operators. This is a **separate dependent admission review**, not a
retroactive promotion of the candidate-only report.

| Property | Immutable source / review |
|---|---|
| Original PDF URL | https://texample.net/files/intersecting-rings.pdf |
| Source provenance | TeXample publisher page; original PDF identified by its SHA-256 (not an invented Git revision) |
| Retained Git blob | Stored at `tests/external_source_corpus/documents/texample.intersecting-rings-v1.pdf` |
| Original bytes | 1,396; SHA-256 `f691ff46e64353ad285a5ddf24c82b956de67b5a6b3ed7d244167a301f36a284` |
| License / author | CC BY-SA 4.0; Dominique Wuertz and TeXample.net |
| Attribution notice | `tests/external_source_corpus/TEXAMPLE-CC-BY-SA-4.0-NOTICE.txt` |
| Rights review | [Issue #262](https://github.com/qigao/QuantaPDF/issues/262) |
| Selected page | Page 0, 238×181 native FIT pixels at 72 DPI |
| Native outcome | `supported-wstar-clip` within the tested narrow C11 profile |

The exact bytes were independently downloaded, length/cryptographic-hash
verified, and then committed without rewriting by the **isolated admission
branch**. The temporary transport workflow is not part of the reviewed
change or a runtime dependency.

The admitted witness is a **separately executed original-PDFium / detached
pure-C11 Native Raster** test: complete original-page RGB mean ≤1.0,
gross (>64) error zero, gray original/native overlap ≥94%, fixed interior
goldens and full RGBA repeat equality. Its W* fill-mask is independently
checked against a source-consistent stroke exclusion region, with an
even-odd/nonzero counterfactual. The tested original has RGB mean
0.842673, gross 0, gray overlap 2,814/2,974, and 3,089 counterfactual
pixels in the current mask. Historical orange counts on different masks
must **not** be compared. No universal pixel-identical PDFium claim follows.

A second `external_texample_wstar_pixel_positive` CTest invokes the
**formal** report mode, which differs from #415's candidate-only report.
The offline admission checksum gate and strict reconciliation require
**seven** exact original documents and **two** independently executed W*
pixel-positive witnesses; both sources' original hashes, legal review
links and actual pixel evidence must match. Missing, swapped, altered or
forged evidence fails closed, and generated 14/36 scenarios never count.

**Release boundary:** master remains **six** admitted original PDFs and
**one** official W* pixel-positive until both PR #415 and this dependent
admission PR have independent approvals, full Linux Release+ASan/UBSan,
macOS/Windows installed SDK/reconciliation evidence, and are merged in
dependency order. No production PDFium fallback, ABI extension or PDF
rewriting is part of this source-admission change.
