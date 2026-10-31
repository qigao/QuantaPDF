# Native V10 generated cross-document corpus (Issue #233)

This is a **test-authored original PDF corpus**, not an external PDF
provenance/license corpus and not a PDFium conformance oracle. No external
assets are downloaded. It is deliberately separate from
`tests/native_source_corpus.def` (historical generated source-family
testing) and `tests/external_source_corpus/manifest.json` (independently
sourced documents with separate license/provenance admission).

## Identity and evidence

- `tests/native/corpus_v10_manifest.json` freezes 12 document IDs, exact
  original-source SHA-256 and byte lengths, generator provenance,
  per-document tags, selected page, expected status and center RGB.
- `tools/qualification/native_v10_corpus.py --mode prepare` builds the
  exact original PDF 1.7 object/xref/stream bytes without using a PDF
  writer that could normalize source evidence. It requires exact pinned
  hashes **before** the native renderer consumes any document. Existing
  modified files are refused rather than silently repaired.
- `quantapdf.native_v10_corpus_replay` uses the normal public C11 facade
  and *one* Context, opening and retiring every source Document on every
  invocation. An independently retained Bitmap remains readable after
  subsequent documents, failures and final Context retirement.
- The C++ receipt records actual per-document success/failure counts and
  permutations. The separate `--mode reconcile` gate verifies it against
  the pinned manifest, exact seeds, portable FNV64 order fingerprints
  and eight synthetic corruption controls. Stale receipts are deleted by
  the prepare stage.

At 72/96/144 DPI, each of 12 originals is executed in canonical order
and two exact xorshift32/Fisher-Yates permutations (seeds `0x4cf5ad11`
and `0x9e3779b9`): **108 executions, 54 accepted and 54 atomically
refused**. Distinct red/green 1x1 images deliberately reuse the resource
name `/I` across *different* documents; clipped blue Image/PATH cases
verify no resource/color/Clip state leakage. Bad originals cover missing
XObject name (FORMAT), late Text, Form XObject, DefaultRGB override,
late Text after Image and unsupported CMYK (UNSUPPORTED). All failed
renders must publish a null Bitmap, never a partial page.

## Scope and non-claims

These twelve small, generated, self-owned originals qualify a **bounded**
Source V10 subset. They do not add native PDF features, a second parser
or raster, broaden the public ABI, relax quotas, or prove arbitrary
real-world PDFs work. Their `reference_oracle` is explicitly `none`:
no PDFium pixels or external copyright/license claims enter this gate.

The independent, provenance-reviewed external PDF intake under #233
remains a separate open work package. Do not count generated originals
as external corpus documents or use an integrity-only manifest as
native rendering evidence.
