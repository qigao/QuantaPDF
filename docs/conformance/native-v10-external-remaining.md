# Native V10 external remaining-source negative intake (Issue #233)

This increment tests the **four other already-reviewed external original
PDFs** after the three-public-document qualification in PR #544. It does not
add any new PDF bytes or modify `tests/external_source_corpus/manifest.json`.
The original external integrity gate must first reverify **all seven files**,
their SHA-256, source revision, declared SPDX licenses and review records.

## Current V10 public API contract under test

| Reviewed external original | Historical manifest profile | Current Native V10 test expectation |
| --- | --- | --- |
| `pdfium.black-original-v1` | supported-path-clip (private Graph/IR) | UNSUPPORTED + NULL Bitmap |
| `pdfium.clip-path-v1` | supported-clipped-stroke (private raster) | UNSUPPORTED + NULL Bitmap |
| `yworks.clippath-cliprule-v1` | supported-wstar-clip (historical oracle) | UNSUPPORTED + NULL Bitmap |
| `texample.intersecting-rings-v1` | supported-wstar-clip (historical oracle) | UNSUPPORTED + NULL Bitmap |

**Do not rewrite historical manifest outcomes.** They describe previously
qualified, different rendering paths, not current public Source V10 support.
The new independent negative report is scoped to the *current public*
`quantapdf_render_page_with_options` API, without PDFium or Graph fallback.

The four originals run at 72 DPI in canonical order and two fixed-seed
`xorshift32-fisher-yates-v1` shuffles. After each rejected original,
the verified `tinytoolkit.sample-blank-letter-v1` is opened in a new
Document using the **same Context**. All 12 negative attempts must return
precisely UNSUPPORTED and publish **NULL Bitmap**; all 12 blank recovery
renders must have exact 612×792 white RGB8. The first blank Bitmap
must remain byte-identical through every intervening negative,
Document disposal and final Context retirement.

`quantapdf.native_v10_external_remaining` writes an actual-execution
receipt only after all assertions pass; the independent
`native_v10_external_remaining.py` gate rehashes the seven unchanged
external originals, validates the integrity-only record, exact statuses,
per-document counts, recovery observations, shuffled orders and fingerprints,
and requires corrupted-report self-tests to be rejected.

## Remaining production gap

The source-wide *Stroke* family remains unsupported in Source V10. This
directly prevents current public qualification for Stroke-dependent
originals, regardless of historical private Composer/Graph render evidence.
Other cases may require qualified Source operator/graphics-state semantics,
larger document geometry or independently audited Clip work. Their precise
causes should be addressed by narrow production feature PRs with original
source proof, **not** by modifying tests to claim unimplemented pixels.

The original `pdfium.black-original-v1` has a historical 612×792
opaque-black private C11 oracle. That evidence is useful as a future
independent source visual expectation but does **not** justify bypassing
public V10 operator admission or relaxing the current 8M sample budget.

This increment changes only tests, receipt checking and documentation.
The unique Native C11 renderer, public ABI, deterministic quotas and
no-PDFium/no-fallback policy remain unchanged. PRs #538 onward stay
Draft/unmerged until dependency/integration gates are fulfilled.
