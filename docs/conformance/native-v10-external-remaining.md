# Current public Native V10: remaining external negative sources

This test contract follows the source-positive work on the
unchanged reviewed `pdfium.black-original-v1` opaque PATH with inert
`/a0 gs` and isolated DeviceRGB Page Group. The former negative intake
also covered black.pdf; that status **must be updated in the staged tests**
when black becomes a production V10 positive. Do not erase prior PR #545's
exact-HEAD historical negative results; they describe the earlier parser.

The remaining reviewed originals and current strict public V10 expectations:

| External PDF | Historical private Graph/Composer expectation | Public V10 |
| --- | --- | --- |
| `pdfium.clip-path-v1` | supported-clipped-stroke | UNSUPPORTED, null Bitmap |
| `yworks.clippath-cliprule-v1` | supported-wstar-clip | UNSUPPORTED, null Bitmap |
| `texample.intersecting-rings-v1` | supported-wstar-clip | UNSUPPORTED, null Bitmap |

The unchanged **seven-file** external corpus manifest and license/source
reviews, hashes and notices remain the prerequisite. A separate opaque-black
positive CTest now owns the public V10 black pixels, not this negative test.

The three remaining originals run at 72 DPI in canonical and
`0x4cf5ad11` / `0x9e3779b9` shuffled order: **9 strict
UNSUPPORTED/null-Bitmap refusals**. Each refused independent Document is
followed by a newly opened and retired verified external
`tinytoolkit.sample-blank-letter-v1`: **9 exact 612×792 all-white
RGB8 recovery outputs**. One Context persists across all requests and
a retained Bitmap survives all other original source Documents and final
Context retirement.

The test writes an actual-execution receipt only after all checks pass.
The independent `native_v10_external_remaining.py` gate rehashes the
seven originals using the existing external verification code, checks the
unchanged integrity-only report and then reconciles exact per-original
statuses, fixed-seed order fingerprints, output counts, recovery and
synthetic corruption-rejection controls.

These three strict refusals do not cancel previous private/C11 positive
evidence; they identify missing **public Source V10** features. Complete
source Stroke/Clip integration under #213/#214 and additional genuine
external PDF pixel acceptance remain open under #233. The single C11
renderer, no-PDFium/fallback policy, resource limits and public ABI remain
unchanged. The stacked feature and test PRs stay Draft/unmerged.
