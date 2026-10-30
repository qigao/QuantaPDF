# Native Source V10: exact TeXample decimal MediaBox — M2 Source-only

Parent: exact-HEAD qualified [Draft PR #560](https://github.com/qigao/QuantaPDF/pull/560), HEAD `10a5dc9a66ad137a1194a9f345ee5e3ec0c0991d`, Native CI #41032281457 SUCCESS 3/3.

## Original geometry evidence

The untouched 1,396-byte `tests/external_source_corpus/documents/texample.intersecting-rings-v1.pdf` (SHA256 `f691ff46e64353ad285a5ddf24c82b956de67b5a6b3ed7d244167a301f36a284`) has original `/MediaBox [0 0 237.133 180.44]`. It is **not** a quarter-point lattice box: the exact selected QPDF Source V10 `source_v10_quarters()` used to reject `237.133` *before* encountering even the first content operator.

This milestone does not round `237.133` to `237.25` or change it to `238`. It adds one extremely narrow exact **1/1000-point** Source geometry branch for the original typed decimal MediaBox operands `0,0,237.133,180.44`, using integer ticks `[0,0,237133,180440]` with denominator 1,000. Every other Source box retains its existing **1/4-point** parser and behavior. Any extra or nonmatching CropBox fails closed; QPDF's independently returned original geometry must exactly agree with the parsed rational ticks. All actual MediaBox, visible-viewport and physical /UserUnit pixel budgets compute with the original fixed denominator and checked integer ceil, without stringifying double.

## Strict ownership and independent test

On this M2 branch, Source V10 may publish a uniquely-owned page IR **only when the full original page content contains NO paint, Image, Clip or text**. The original physical page dimensions in that IR are the source's actual `237.133` and `180.44` (the binary32 representation of those exact decimal rationals); **not** a fabricated 238×181 Composer geometry. The page has precisely zero operations and resources. It is a **private Source geometry acceptance**, not a claim of public blank-page RGB support: the existing C11 device projection still correctly rejects page geometry that it has not independently normalized to its own exact decimal planner.

The *existing* in-tree `quantapdf.native_source_stage_oom` CTest, compiled against the same production Source unit, now authors original PDF byte streams with QPDF and tests:
- An empty original exact MediaBox; a balanced `q 1 0 0 1 118.566 90.22 cm Q` state-only source, and an identical explicit CropBox. All are **Source-IR-positive**, preserving the source decimal width/height and allocating no paint/Clip/GS resources.
- Failure of the real first C-owned Source allocation requires `NOMEM` with **all-zero IR** and a separately re-parsed success.
- Nine strict atomic negatives: either **0.001** altered media dimension, nonmatching CropBox, nondefault UserUnit, page rotation 90, original-color Fill, Text, unbalanced q/Q, and unsupported XObject Do.
- The unmodified original TeXample **remains `UNSUPPORTED/null Bitmap`** in the public external corpus test #49/#50. It has 8 complete four-cubic contours = 32 original `c`: 2 W* Clip, 2 orange f*, 2 first black S, 2 second black S, plus original empty `m` subpaths and `0.3985 w`.

The authenticated original CTM is accepted as **state-only with no paints** in this test. That does NOT prove original painted Bézier coordinates under this CTM, nor source-level lossless empty-subpath retirement, and no such ability is exposed. The old QPDF Source reading, Source C allocation hooks, public API, SDK exports, C11 renderer, its 8,000,000 edge-sample cap and existing CI configuration are unchanged except the stated Source-only parser/geometry acceptance and reuse of its existing Source-stage OOM test. No consumer/installer or CI test target is added.

## Next independent gates

1. Admit the exact original five-decimal affine translation and real quadratic/quarter-grid-breaking path coordinates under the special **millipoint** Source staging contract; verify all zero-edge `m` semantics without deleting a real edge.
2. Stage the actual 8×4=32 original cubic commands into bounded individually owned Clip/Path source structures; no opportunistic generic 12-command Clip widening.
3. Connect genuine source `W* n → orange f* → two 0.3985px black S`, with real q/Q state, per-paint Clip membership and real curve Stroke/joins under the same V5→V1 C11 4×4 sampler and unchanged quota.
4. Finally qualify the unmodified original PUBLIC 72-DPI RGB, C allocation OOM, full layer cost and SHA-pinned external corpus reconciliation; until then TeXample stays an atomic public negative.

Keep #213/#214/#233/#530 OPEN, all PRs Draft/unmerged, master unchanged.
