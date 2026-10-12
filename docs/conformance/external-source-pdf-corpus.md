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

The repository now contains two externally sourced CC0 PDF documents:
one strict text `UNSUPPORTED` example and one blank-page positive baseline.
The generated 14 families / 36 PDF executions are unaffected, and no
external document is generated or downloaded in CI.

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
`tools/qualification/reconcile_external_corpus.py` **after** both external
source CTests. This gate verifies the separate offline admission report
against actual original-PDFium/native semantic JSON for both pinned
documents. A manifest's expected capability, PDFium's successful rendering
alone, or merely having a JSON file can **never** increment a native
Path/Clip pass counter. It fails on missing/swapped IDs, altered byte
admission status, missing test success, nonzero blank pixel error,
unjustified painted Path/Clip counts, or leakage from the 14/36
QPDF-generated fixture totals.

The published artifact `external-corpus-reconciled-{linux,macos,windows}`
currently records two admitted documents, one expected text rejection,
one independently rendered blank page, and **zero** external native painted
Path/Clip matches. This reconciled evidence is the denominator and baseline
for #254; it does not replace future true painted Path/Clip acceptance and
original-source PDFium pixel parity.
