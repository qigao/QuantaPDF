# M2 rectangular Stroke, W Clip and Fill qualification

PR #392 is stacked on the M1 device plan branch. It remains Draft / DO NOT
MERGE until exact-head cross-platform and installed-SDK checks are reviewed.
This private Native implementation does not switch the public SDK backend,
add a fallback, or admit the original into the official corpus.

The optional original-source gate uses the unchanged 3,344-byte jsPDF
`w3s_clip.pdf` pinned by `tools/qualification/native_device_original.py`.
It preserves both Path operations. Source and Graph ownership end before
rasterization; detached IR is disposed before image comparison. Rendering
twice must produce identical RGBA bytes and leave the device plan unchanged.
The source hash is checked before and after execution.

## Independent geometry and samples

Coordinates below are in points from the page's top-left. They are literal
source constants, not bounds read from Graph, detached IR or the device plan.

| Shape | Bounds |
| --- | --- |
| Page | 595.28 × 841.89 |
| Black Stroke centerline, width 1 | (50, 20)–(250, 140) |
| Stroke outer rectangle | (49.5, 19.5)–(250.5, 140.5) |
| Stroke inner rectangle | (50.5, 20.5)–(249.5, 139.5) |
| Nonzero W Clip | (50, 20)–(250, 140) |
| Red Fill before clipping | (0, 0)–(150, 100) |
| Visible red intersection | (50, 20)–(150, 100) |

At 72 DPI the FIT grid is 596 × 842; at 96 DPI it is 794 × 1123.
Every pixel is checked against an independent integer-rational 4×4 sample
oracle. The black ring coverage and clipped red coverage are quantized to
8-bit alpha separately, then composed red over black. Every RGBA channel must
match. The original grids have no sample-center ties; a tie fails this gate.
The checks require nonempty red Fill, black Stroke, transparent outside and
white Clip-interior regions, not just the presence of two colors somewhere.

## Full RGB comparison with PDFium

PDFium renders the same unchanged source twice into the same FIT grid on a
white background. Native RGBA is independently composited on white for the
comparison. PDFium is a test oracle only.

All channels must match exactly away from source-derived boundaries. A
boundary pixel is one whose closed unit square touches a visible Fill or
Stroke outer/inner rectangle boundary, calculated using integer arithmetic.
For a boundary channel with independently predicted Native intensity `v`,
the allowed delta is `max(v, 255-v)`: PDFium can snap coverage toward either
endpoint. Red still matches exactly on Fill-only edges. The full-image L1
budget is the sum of those channel envelopes. The differing-pixel count
cannot exceed the boundary-pixel count. No measured residual, Native pixel
value or renderer-derived geometry sets a tolerance.

Report schema v3 records these metrics and explicitly distinguishes bounded
full-RGB qualification from byte-identical PDFium pixels. At 144 DPI the
1,191 × 1,684 grid exceeds the unchanged 1,000,000-pixel limit: plan and IR
must be zero, and the raster must not be invoked.

## Clip associations and negative controls

Each scoped operation uses its immutable graphics-state Clip association.
The original Stroke has state ID zero; the later Fill refers to the frozen
W Clip. A Stroke authored inside an existing Clip keeps it. Two Paths and
one Clip do not establish execution order. A regression compares both the
scoped and direct raster against an independent red/white image when the
entire Stroke is outside their explicitly associated Clip.

Negative controls start from a successfully admitted rectangular Stroke.
Invalid widths (zero, negative, NaN, infinity and unsupported width 2),
shear, a nonrectangular quadrilateral, insufficient edge/sample budgets and
over-budget output must reset a nonzero output sentinel to zero. Curved and
open strokes remain unsupported. Temporary production stderr tracing is
removed; test failures retain contextual diagnostics.
