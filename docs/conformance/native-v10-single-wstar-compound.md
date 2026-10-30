# Public Source V10: one compound W/W* Clip leaf (Issue #233)

This stacked Draft increment supplies a **bounded implementation step**
toward the independently sourced [yWorks clip-rule PDF](https://github.com/yWorks/svg2pdf.js/blob/b564556f41b245893fbadd3a6c4f832455383dbe/test/clippath/clippath-cliprule.pdf)
(SHA-256 `9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783`).
The yWorks 3,747-byte original is **not** changed or yet claimed as
public Native V10 pixel-positive. It already has separate reviewed MIT
provenance and historical private C11 oracle evidence.

## Why this is a separate admission step

The historical private yWorks result uses an original **600x600** page,
**3 painted PATHs**, **two W leaves and two W* leaves**, **7 Clip DAG
nodes** (3 intersections) and **3 Graphics States**. The current
public Source V10 supports a smaller bounded Clip graph and an 8M
sample-check budget; merely enabling `W*` or copying old private
Graph/Composer output would not prove the entire source correct.

The public source parser already constructs and owns a single Clip
leaf containing **one or two closed contours**, with at most **nine
source commands**. It freezes authored PATH coordinates, the original
`W` / `W*` winding rule and GS membership. The missing connection
was the one-Clip C11 V5/V1 *device consumer*: it had a strict
single-contour admission for this entry even though a separate
two-GS compound entry had independently qualified the same two-contour
geometry.

## Implemented profile

- One source PDF Page, one frozen Clip leaf and one bounded Normal,
  full-alpha GS; one to four owned, opaque PATH Fill layers within
  that single scope. This increment does **not** add another Clip leaf,
  intersection root, GS, source PATH operation or sampling budget.
- The Clip has at most nine commands in **one/two closed contours**.
  Every contour has exactly one MOVE, at least two LINE/CUBIC edges,
  and a CLOSE; the already bounded QPDF source parser retains it in
  owned C11 data. The **same** 4x4 V1 winding sampler computes
  nonzero `W` versus even-odd `W*` in one complete winding check.
- Private dispatch chooses the compound one-leaf case only when an
  actual second MOVE occurs in the owned Clip. The new private C11
  wrapper validates one leaf, full correct source identity, resource
  capacities and complete contour grammar before output. Ordinary
  one-contour and two-GS entrypoints keep their existing admission.
- Test-only original PDF fixtures (not external yWorks originals)
  contain two nested triangles with the SAME winding. A 32x32
  red/blue Fill must show the outer lobe under both `W` and `W*`,
  but the interior center must be opaque under `W` and **white under
  `W*`**. Reversed inner winding must instead remove the center
  under `W`. Test-owned PDFs verify RGB8, identity on repetition,
  a detached Bitmap after all Document and Context destruction and
  strict atomic null-Bitmap refusals for three contours, late Text,
  late Stroke, unqualified resource/Clip operations.
- Resource budgets, a fixed four-layer C11 compositor, 4x4 sample
  work, byte lifetime and Windows installed ABI stay unchanged.
  Neither qpdf nor PDFium participates in actual pixel production.

## Remaining yWorks external-source work

**Keep** `yworks.clippath-cliprule-v1` as an **UNSUPPORTED + NULL
Bitmap** current public V10 original in the independently reconciled
seven-source external corpus. Its full multi-Clip, three-GS, 600x600
resource geometry and all three painted operations require separately
bounded source admission and C11 execution with adequate work
accounting **inside existing limits**. Positive status cannot be
inferred from this 32x32 generated fixture or old private pixel
parity. TeXample's independent complex cubic Stroke remains a
separate open qualification. Keep the dependency stack Draft and
Issues #233/#214/#213 open.
