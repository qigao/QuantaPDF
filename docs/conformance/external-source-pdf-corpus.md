# External PDF conformance corpus: admission contract (v1)

Tracks [#233](https://github.com/qigao/QuantaPDF/issues/233),
[#229](https://github.com/qigao/QuantaPDF/issues/229), and the
PDFium-free product exit gate [#208](https://github.com/qigao/QuantaPDF/issues/208).

## Baseline: zero external PDFs

The first version of `tests/external_source_corpus/manifest.json` deliberately
contains `"documents": []`. **No external PDF has yet been admitted or
qualified**. The existing generated corpus remains **14 families / 36
synthetically generated PDF executions**, tracked separately in
`tests/native_source_corpus.def`. Never add these counts together as a
compatibility score or characterize an empty manifest as external conformance.

An external PDF must be a pre-existing document with an independently
recorded source and its own copyright/licensing status. A repository's
open-source *code* license does not necessarily license every attached
PDF test file. No external PDF is silently downloaded, redistributed, or
passed off as licensed merely because its URL is public.

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

The CLI produces an atomic, deterministic JSON report. With the initial
empty manifest its result is `no_external_documents`,
`document_count: 0` and `native_conformance_passed_count: 0`.
If documents are added and pass the integrity checks, the result is
`integrity_verified_only`: **this is not yet a native compatibility
pass**. The per-document expected native outcome is declarative, not
executed by this admission tool. The binary SHA-256 measures *exact source
bytes*, not canonicalized PDF semantic equivalence.

The tool never substitutes PDFium or qpdf rendering for native results,
and cannot change QuantaPDF's public SDK dependency graph.

## Procedure to admit the first licensed external PDF

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
