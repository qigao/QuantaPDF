# Native source-PDF conformance corpus: bounded generated fixtures

Tracks [#233](https://github.com/qigao/QuantaPDF/issues/233),
[#229](https://github.com/qigao/QuantaPDF/issues/229),
[#214](https://github.com/qigao/QuantaPDF/issues/214) and the
PDFium-free exit gate [#208](https://github.com/qigao/QuantaPDF/issues/208).

## Executable manifest

The versioned registry at [`tests/native_source_corpus.def`](../../tests/native_source_corpus.def)
is the single source of truth for **20 fixture families / 58
generated source-PDF executions** following the Clip, resource-negative and
independent resource-ancestry-budget qualifications. The original baseline was 14/36; these are generated fixtures,
not additional licensed external originals. It carries a stable family ID, feature
tags, expected accepted/rejected/mixed outcome, oracle category, number of
generated documents, and the test function bound to each row.

The existing `quantapdf.native_source_qq_oracle` CTest binary compiles
this registry and runs **every** listed family in three deliberately different
orders: canonical manifest order and two fixed-seed Fisher–Yates
permutations (`0x4cf5ad11` / `0x9e3779b9`, xorshift32-v1). Each order
executes all 20 families and all 58 generated fixtures once. The runner
verifies an exact one-to-one family permutation, declared/observed
per-family counts, absence of leftover temporary source PDF and matching
repeat coverage after each order. Thus it performs **58 canonical + 116
replayed = 174 actual fixture executions**, not 174 new or distinct
documents. An independent Python CTest validates the JSON against the
versioned registry, including the exact seed-derived orders. No second
source oracle binary or GitHub Actions workflow is needed.

This is an **in-repository generated corpus**, not a claim that 58
independent external documents have been qualified. A family with two
variations has two generated PDFs; a two-page fixture still counts as one
generated PDF. The individual assertions within each family retain their
exact status, budgets, deterministic repeated render and color checks.

## Deterministic JSON results (CI artifact)

The existing CTest target invokes its test binary with
`--corpus-report <build-directory>/tests/native-source-corpus.json`.
The report has `schema_version: 2`, `corpus_id: native-source-pdf`,
`fixture_kind: generated`, `result: passed`, and backward-compatible
canonical `family_count=20` and `fixture_count=58` fields. The ordered
`family_results` array preserves each ID, tags, outcome/oracle category,
declared/observed canonical count and pass status. New `order_validation`
records the fixed algorithm, two replay seeds, the ordered family IDs for
all three runs, 116 replay executions and 174 total executions. All
strings are JSON escaped; the exact orders are verified independently
from the registry by `tests/test_native_source_corpus_report.py`.

The file contains no timestamps, runner paths, randomized identifiers or
QPDF output-byte hashes. On a new invocation, any old JSON and temporary
file are removed; a failing fixture or report write cannot leave a new
published success JSON. The final JSON is renamed from a temporary file
only after all three execution orders have passed, every family execution
count matches its declaration, and the output is fully written. The
separate Python gate depends on the source-oracle CTest and rejects stale
schema-v1 reports, missing/malformed orders, incorrect fixed seeds,
partial replay counts or duplicates.

The **same** QuantaPDF CI workflow uploads platform-specific artifacts
under `source-corpus-linux`, `source-corpus-macos`, and
`source-corpus-windows` (seven-day retention). Linux includes separate
Release and ASan/UBSan reports. Missing output is a CI failure on a
successful job. These artifacts are machine-readable **family-coverage
results**, not per-PDF pixels, externally sourced PDFs, or PDF file hashes.
The fixed pixel thresholds and explicit unsupported contracts remain
enforced by their existing executable tests.

## Move-only Clip source qualification

The three generated families `clip.move_only_source`,
`clip.degenerate_line_source` and `clip.move_only_multistream` expand
original-PDFium versus detached native C11 parity to source `m W n` /
`m W* n`, `m h W n`, a coincident-point `l`, a definition-time CTM
distinct from later painting, and a `W*`/ `n` operator split across two
`/Contents` streams. The original source document and Graph snapshot are
released before native rendering. Every generated source uses full-page
RGB tolerance, fixed interior pixel goldens and exact repeat RGBA equality.
The separate no-constructor `W n`/`W* n` behavior remains a no-op based on
the existing PDFium-observed source test; a MOVE-only path is an actual
zero-area clipping path. This is bounded path-only qualification, not a
general original-PDF compatibility or production PDFium cutover.

## Referenced graphics-resource negative admission

Two resource-specific generated families extend the fixture registry:

- `resources.invalid_graph_matrix` (14 generated source documents; status-only):
  missing `/Resources`, absent or wrong-typed `/ExtGState`, unresolved `/GS`,
  non-dictionary states, incorrect `/Type`, nonnumeric/out-of-range alpha,
  unadmitted blend modes and soft masks, unknown `/OP`, and page-level
  `/Resources` masking otherwise-valid ancestor resources. A valid PATH paint
  precedes each invalid `gs` operator; parsing/lowering still must return the
  exact expected error and publish **zero** detached output fields. Repeated
  capture must not reuse failed resource or Graph state.
- `resources.late_multistream_isolation` (one original, two selected pages;
  mixed): a parent owns an indirect valid `/GS`, but the second page has an
  independently indirect, unsupported `/GS` with the same name. The bad page
  splits `/GS` from `gs` across two `/Contents` streams after an earlier
  valid PATH. It fails atomically without fallback to the parent's definition.
  The valid sibling is independently compared against **direct original
  PDFium** on full-page RGB and fixed color probes, survives destruction of
  source/Graph owners, and renders byte-identically before and after the
  sibling's failures. The stream-count limit is separately enforced.

The matrix is deliberately a **referenced** resource test, not blanket refusal
of unknown but unused PDF resource dictionaries. It does not add a public
renderer feature, expand the native capacity limits or affect the formally
admitted external-source count, which stays at **7/2**. The new seeded replay
gate tests order independence deterministically, but does **not** imply
random fuzzing, mutation campaigns or full multi-document lifetime proof;
those remain scoped follow-ups under #233.

## Independent resource PageTree depth qualification

The `resources.parent_depth_budget` generated original uses a three-node
`Page → Pages → root Pages` chain with root-owned indirect `/Resources`
and referenced `/ExtGState /GS`. The caller explicitly budgets three
parent nodes, independently of decoded stream/direct nesting. The same
`max_parent_steps` now bounds both geometry and resource ancestor
lookup: a **two-step** ancestry limit refuses even if decoder depth is
ample. Conversely, a **two-level decoder replay** refuses even when three
ancestor nodes are admitted. Both sufficient budgets allow the named GS,
a detached Native Path/Clip image and original-PDFium RGB/interior
parity plus repeat-byte identity after Source/Graph destruction.

The private page semantics API accepts `max_parent_steps` explicitly,
rather than borrowing `decode_limits.max_direct_depth`, which remains a
separate stream decoder bound. This is not a public API change or a
permission to traverse arbitrary resource ancestry.

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
[external-source-pdf-corpus.md](external-source-pdf-corpus.md). The manifest now lists **one independently published CC0 PDF**, admitted
by exact SHA-256 and recorded document-level license evidence in [#250](https://github.com/qigao/QuantaPDF/issues/250).
Its initial *native* qualification was a strict **UNSUPPORTED** text
rejection with original-source PDFium reference rendering. The external
corpus has since been independently extended to **7 unchanged admitted PDFs
/ 2 W* original-source native-pixel positives** (see #262 and its
post-merge master qualification). These are never included in the **58 canonical generated-source
executions**, nor in the 116 verification replays. A failed test must never be converted to a
pass by loosening pixel thresholds or silently redirecting to PDFium.

The public QuantaPDF SDK remains PDFium-backed. Remove its PDFium
link/install dependency only after the full independent compatibility
exit gate in #208, not from this generated Path/Clip subset.
