# #269 — Exact residual pixels after repeated-1pt-Stroke preview

This is a **test-only** follow-up stacked on the CI-green #284 private
repeated-horizontal-Stroke candidate. It does not change C11 production
rasterization, the original byte-pinned jsPDF file, reference PDFium
rendering, any ABI, public SDK, release or formal source corpus.

## Why the remaining pixels matter

The unchanged original 596x842 PDF contains a **triple-traversed top
1pt Stroke** in its first S Path and a **single-traversed bottom 1pt Stroke**.
The old bounded C11 stroke treated the repeated segments as a coverage
union, underestimating the top horizontal pixels. The first experimental
'fill every thin horizontal scan row' preview was rejected: total original
RGB error increased to 174,438 from the unchanged baseline 141,465.

The revised explicitly-invoked #284 candidate only considers overlap of
at least TWO horizontal source segments of one Stroke, exact width=1pt and
projected width less than two device rows. The original source S and
later W-clipped red Fill are composed **in source order** and compared
to the **same raw PDFium original** at all 501,832 pixels / 1,505,496
RGB samples. Full CI confirmed Linux Release+ASan/UBSan, macOS and
Windows identical values:

| Full-image metric | Old private V0 | Repeated-only experimental preview |
| --- | ---: | ---: |
| Total RGB absolute difference | 141,465 | 27,735 |
| Mean absolute RGB difference | 0.093965710 | 0.018422500 |
| Differing pixels | 1,360 | 1,062 |
| >64-delta pixels | 301 | 2 |
| Max delta | 127 | 127 |
| Candidate white-RGB FNV64 | 3049e6766a5abd98 | 08727d7a539b343a |

The **80.4%** absolute error reduction and 299 fewer gross mismatches
are significant but **not** pixel-parity certification. The new preview
is not the default, and the two original whole-document negative entry
points still return UNSUPPORTED and publish zero pixels.

## New executable spatial proof

The existing pure test helper
`tests/original_jspdf_diff_attribution.h` rereads **all** 596x842
original PDFium-vs-new-C11 pixel pairs, this time using the isolated
candidate Stroke and the unchanged borrowed W-clipped Fill.

Machine-checked JSON records, in four native source-alpha categories
(flat, Stroke-only partial, Fill-only partial, both partial):
- changed-pixel counts, >64 channel-delta counts, and total RGB
  absolute error; all four totals must match the *independently*
  computed candidate full-image metrics;
- the four source-alpha group's changed-pixel bounds and 3x3
  native-flat-neighbor split;
- up to twelve sorted worst source-device pixel coordinates with
  candidate vs PDFium RGB, stroke alpha, fill alpha and per-pixel
  maximum channel/aggregate error.

The CMake checker rejects missing, malformed or inconsistently ranked
sites. It also requires the measured candidate to improve both original
all-RGB error and gross-error count against the unchanged baseline.
This is **an A/B regression gate, not a general acceptance tolerance**.

The next engineering change must be based on the actual two remaining
large-mismatch coordinates (likely boundaries, joins or cap interaction),
not on a universal pixel shift, invented PDFium antialias sampling
centers, new source bytes or arbitrary threshold relaxation.

Keep #269 Open, this PR and all stacked parents Draft / DO NOT MERGE,
formal external corpus/W* positive counts unchanged and public SDK
PDFium-backed.
