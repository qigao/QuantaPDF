# Public Native V10 — remaining complex external originals

This updated child-branch profile follows the exact public Source V10
admission of **two more genuinely painted reviewed originals**:
`pdfium.black-original-v1` (opaque Group/gs black PATH) and
`pdfium.clip-path-v1` (bounded W/n clipped 1px blue Stroke). These
positives have separate, explicit *actual public* C11 RGB tests; do
not erase the earlier exact-HEAD Draft #545 negative evidence, which
described the narrower earlier Source V10 implementation.

The only **two** remaining reviewed external originals currently
rejected by public V10 are:

| External PDF | Historical private profile | Public V10 |
| --- | --- | --- |
| `yworks.clippath-cliprule-v1` | supported-wstar-clip | UNSUPPORTED / NULL Bitmap |
| `texample.intersecting-rings-v1` | supported-wstar-clip | UNSUPPORTED / NULL Bitmap |

The unchanged **seven-source** external manifest and its source URLs,
license-review references and SHA-256 hashes remain the prerequisite.
The two negative originals are run at 72 DPI in canonical order,
then `xorshift32-fisher-yates-v1` orders seeded `0x4cf5ad11`
and `0x9e3779b9`. Only **two distinct permutations** of two IDs
exist; the third pass necessarily repeats one. The receipt records
and independently checks the exact seed/order/fingerprint for every
pass, without inventing three distinct orders.

The six negative public render calls must all return **UNSUPPORTED
with NULL Bitmap**. After each negative, the already reviewed
`tinytoolkit.sample-blank-letter-v1` is independently opened,
rendered as exact 612×792 all-white RGB8, and retired under the
same Context (**six successful recovery paints**). A retained Bitmap
survives every intervening Document and final Context retirement.

The separate `native_v10_external_remaining.py` checker first
re-verifies all seven unchanged original PDFs with the existing
`external_pdf_corpus.py` verifier. Its `integrity_verified_only`
result still reports zero native conformance *by itself*. The checker
then reconciles the actual independent C11 execution receipt, exact
negative statuses, recovery pixels and owner lifetime, fixed-seed
fingerprints and corrupted-report refusals.

Unqualified Source operations and Stroke/Clip semantics outside the
new bounded triangle profile must still fail atomically. There is
only one PDFium-free Native C11 renderer, no fallback, no raised work
limits and no added public ABI. Source/C11 feature expansion remains
open under #213/#214/#233; all stacked PRs stay Draft/unmerged.
