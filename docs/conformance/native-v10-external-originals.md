# Native V10: external original-PDF admission (Issue #233)

This qualification uses **unchanged, independently sourced original PDFs**
already checked into `tests/external_source_corpus/documents/` with source URLs,
pinned source revisions, exact SHA-256, declared SPDX license and project issue
license-review links. Do not download a substitute file, generate a replica,
or rewrite the original external PDF.

## Separate evidence paths

The first CTest invokes the existing
`tools/qualification/external_pdf_corpus.py` over the entire
`tests/external_source_corpus/manifest.json` with **seven** declared files.
Its result is **integrity_verified_only** with
`native_conformance_passed_count = 0`: metadata and hashes are prerequisites,
**not** rendering success.

Only after that prerequisite does
`quantapdf.native_v10_external_replay` open the unchanged originals through
the **public C11 Native V10** API and independently check actual status,
null Bitmap on refusal, and full RGB pixels on an admitted page. The test
qualifies exactly three document IDs:

| Original document | Manifest's historical expected outcome | Native V10 public expectation |
| --- | --- | --- |
| `tinytoolkit.sample-blank-letter-v1` | supported-blank-page | OK, 612 × 792, every RGB channel 255 |
| `tinytoolkit.sample-1-page-v1` | unsupported | UNSUPPORTED, null Bitmap |
| `pdfium.wstar-late-text-v1` | unsupported | UNSUPPORTED, null Bitmap |

All three use page index zero at 72 DPI in canonical order and two separately
seeded `xorshift32-fisher-yates-v1` orders. That is **9 real render calls:
3 blank positives and 6 strict atomic refusals**. Each original Document is
opened and retired independently under the same Context; a native Bitmap from
the blank page remains readable after other originals, refusals and Context
retirement.

The third CTest invokes
`tools/qualification/native_v10_external_reconcile.py`: it runs the **same
seven-source SHA-256 and review-metadata verification again**, requires the
original integrity-only report to match, and validates the *separate* native
replay receipt's exact source IDs, three-pass order fingerprints, outcomes,
counter types and no-reference-oracle property. Built-in deliberately
corrupted receipts must be refused.

## Scope boundary

**Four other external originals are explicitly NOT qualified by this
increment**: `pdfium.black-original-v1`, `pdfium.clip-path-v1`,
`yworks.clippath-cliprule-v1`, and `texample.intersecting-rings-v1`.
Historical positive/negative PDFium or private-IR evidence for these files
does not automatically establish public V10 pixel correctness.

No production PDF renderer/parser, public ABI, quotas, fallback, PDFium build
or installed package changes. The real external corpus is entirely distinct
from the generated `native-v10-cross-document-v1` test-owned originals.
Issues #233, #214 and #530 remain open for the additional external features,
font/stroke/Clip parity, stronger source provenance checks, and future
feature-specific C11 renderer acceptance.
