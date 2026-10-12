# Native source-PDF conformance corpus: generated baseline v1

Tracks [#233](https://github.com/qigao/QuantaPDF/issues/233),
[#229](https://github.com/qigao/QuantaPDF/issues/229),
[#214](https://github.com/qigao/QuantaPDF/issues/214) and the
PDFium-free exit gate [#208](https://github.com/qigao/QuantaPDF/issues/208).

## Executable manifest

The versioned registry at [`tests/native_source_corpus.def`](../../tests/native_source_corpus.def)
is the single source of truth for the initial **14 fixture families / 36
generated source-PDF executions**. It carries a stable family ID, feature
tags, expected accepted/rejected/mixed outcome, oracle category, number of
generated documents, and the test function bound to each row.

The existing `quantapdf.native_source_qq_oracle` CTest binary compiles
this registry and runs **every** listed family. Each generation site records
one fixture execution. The runner fails on duplicate/empty family IDs,
unknown expectation/oracle categories, missing tests, wrong per-family
counts, or incorrect aggregate coverage. It prints one stable
`[source-corpus] PASS` log record per family and a completion total. A
new family must update the executable registry; an expanded family must
update its declared count and the aggregate gate. No second test binary or
GitHub Actions workflow is needed.

This is an **in-repository generated corpus**, not a claim that 36
independent external documents have been qualified. A family with two
variations has two generated PDFs; a two-page fixture still counts as one
generated PDF. The individual assertions within each family retain their
exact status, budgets, deterministic repeated render and color checks.

## Deterministic JSON results (CI artifact)

The existing CTest target invokes its test binary with
`--corpus-report <build-directory>/tests/native-source-corpus.json`.
The report has `schema_version: 1`, `corpus_id: native-source-pdf`,
`fixture_kind: generated`, `result: passed`, and aggregate
`family_count` and `fixture_count`. The ordered `family_results` array
contains the family ID, feature tags, expected outcome, oracle category,
declared and **observed** fixture counts, and pass status. All string fields
are JSON escaped.

The file contains no timestamps, runner paths, randomized identifiers or
QPDF output-byte hashes. On a new invocation, any old JSON and temporary
file are removed; a failing fixture or report write cannot leave a new
published success JSON. The final JSON is renamed from a temporary file
only after every test has passed, every family execution count matches its
declaration, and the output is fully written.

The **same** QuantaPDF CI workflow uploads platform-specific artifacts
under `source-corpus-linux`, `source-corpus-macos`, and
`source-corpus-windows` (seven-day retention). Linux includes separate
Release and ASan/UBSan reports. Missing output is a CI failure on a
successful job. These artifacts are machine-readable **family-coverage
results**, not per-PDF pixels, externally sourced PDFs, or PDF file hashes.
The fixed pixel thresholds and explicit unsupported contracts remain
enforced by their existing executable tests.

## Oracle and acceptance boundaries

| Oracle label | Meaning |
|---|---|
| `original-pdfium` | The **original source PDF** is independently rendered by test-only PDFium and compared with native pixels (whole-image and/or interior golden checks), with appropriate positive/negative assertions |
| `native-only` | Graph-to-detached-IR and native execution are verified without an original-PDFium pixel comparison for that fixture |
| `status-only` | Malformed or unsupported source must return the asserted error with no partial IR publication |

`accepted` / `rejected` / `mixed` are family-level expected outcomes;
mixed cases deliberately contain both a supported selected page and a
rejected page/resource/budget configuration. These names **do not** imply
arbitrary PDF syntax support.

For whole-image oracle comparisons, the current fixed tolerance is mean
RGB channel difference <= 10 and gross pixel fraction (any channel delta
> 64) <= 1/12. Deterministic repeated native RGBA results are compared
byte-for-byte. Individual tests further pin interior goldens and fixed
page geometry. Rejections are explicit: no PDFium fallback, no automatic
retry and no partial IR.

Existing C11-native-only Clip/Path tests remain provider-free. PDFium and
QPDF are linked **only into the source-oracle test target**, not the
private C11 native raster implementation.

## Provenance, determinism and limitations

Fixtures are synthesized from fixed code paths through the test-only QPDF
writer, with no random input or remote file retrieval. The stable IDs and
feature tags identify **semantic fixture families**. They are not SHA-256
hashes of exact PDF bytes: QPDF serialization can vary by library version
or output metadata. Do not treat generated PDF binary hashes as
cross-platform goldens until output canonicalization and toolchain
provenance have been qualified.

This baseline provides an executable inventory rather than a broad public
PDF compatibility score. It does not count the entire standalone native
test suite, and it deliberately includes one `native-only` fixture.
An independently tracked **external PDF** corpus now has an offline
provenance/byte-integrity admission gate documented in
[external-source-pdf-corpus.md](external-source-pdf-corpus.md). Its initial
manifest contains **zero** admitted documents. That gate never adds to the
36 generated-source executions and never claims native renderer parity;
actual licensed external PDFs and their semantic pixel/status qualification
remain future #233 work. A failed test must never be converted to a pass by loosening
pixel thresholds or silently redirecting to PDFium.

The public QuantaPDF SDK remains PDFium-backed. Remove its PDFium
link/install dependency only after the full independent compatibility
exit gate in #208, not from this generated Path/Clip subset.
