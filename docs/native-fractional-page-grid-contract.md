# #269: fractional-page grid contract, geometry stage only

Status: tests of the existing private C11 RenderPlan; no native raster admission
change. This work is stacked on #270's immutable jsPDF negative. The original
Source -> Graph -> Detached IR may lower while Scoped Raster still returns
UNSUPPORTED and publishes zero pixels. The public SDK remains PDFium-backed.

## Three distinct values

Keep original PDF geometry, displayed page points, and integer device output
separate. Never replace a fractional MediaBox with an integer box just because
the device needs integer width/height. Raw CropBox offset, intrinsic /Rotate,
UserUnit and the y-axis convention belong to the geometry producer. The planner
receives the displayed extent and an already-normalized source transform. A
render-options clip is in displayed points and is NOT a raw PDF CropBox.

The current shared planner (`src/render/render_plan.c`) calculates an outward
floor/ceil rectangle around rotated/scaled relative clip bounds. It checks int,
stride and size_t representability and clears the output on failure. It does
not itself impose the native raster's pixel/edge/sample-work budgets; those
must remain independently enforced before allocation and work in #269.

At zero additional rotation the aligned draw rectangle uses
`start_x=floor(-clip_x0*dpi/72)`, `size_x=ceil(page_width*dpi/72)`, with analogous
Y values. The generic float matrix instead preserves the point-based scale
and subpixel clip translation. These existing execution paths are not
interchangeable merely because their output dimensions agree.

For 595.28 x 841.89 points, explicit expected planner extents are:

| DPI | Width | Height |
| --- | --- | --- |
| 72 | 596 | 842 |
| 96 | 794 | 1123 |
| 144 | 1191 | 1684 |

The aligned rectangle analytically implies axis ratios 596/595.28 and
842/841.89 at 72 DPI, rather than uniform 1:1 point sampling. The new tests
check this distinction and algebraic pixel-center inverse mapping. They do
NOT independently measure PDFium's actual matrix or antialiasing sample grid.
That evidence still requires the original-PDFium integration oracle.

## Executable contract

`tests/test_fractional_page_grid_contract.h` is invoked from the existing
`quantapdf_test_render_plan` main, which already compiles the real planner as
C11. No duplicate planner implementation, provider fallback, new workflow or
extra dependency is added to the repository.

Six groups cover fractional extents at three DPIs in RGB/RGBA, repeated planning
and float narrowing for this A4 geometry; aligned-vs-point mapping; positive and
negative fractional render-clip origins; 90/180/270/-90-degree corner mappings
and inverse pixel-center algebra; synthetic producer normalization including
nonzero origin, y-flip and UserUnit=2; and precision/overflow failure reset.
The UserUnit/origin cases are arithmetic fixtures, not original-PDF evidence.
No additional CTest count or formal-corpus count is claimed for these subcases.

An important counterexample is explicitly retained: planning width
595.000001 in double gives 596 pixels at 72 DPI, while first narrowing that
number to float gives 595 pixels. This is a planner precision counterexample,
NOT a demonstrated PDFium bug or a demonstrated failure of the jsPDF original.
Any future admission rule must use the actual source/provider precision
contract; it cannot infer universal original-device parity from a float extent.
The jsPDF-sized cases remain stable for the three DPIs tested here.

## Remaining acceptance work

1. Independently observe original-PDFium mapping at selected device points and
   explicit DPI/rotation/clip settings, separating intrinsic and requested
   rotation. Compare coordinate evidence before attempting pixel parity.
2. Define a bounded private native point-to-device mapping and precision
   admission policy; preserve original and owned IR geometry. Check pixel,
   stride/output-byte, edge and sample-work budgets before publishing anything.
   Integer representability alone is not a bounded raster-work admission gate.
3. Only then integrate it into the pure C11 raster and compare the unchanged
   jsPDF original against original PDFium: full image, fixed interior colors,
   repeat output and resource-limit failures on Linux Release + ASan/UBSan,
   macOS and Windows. The #270 negative changes only with that explicit gate.

No new formal original, native pixel positive or W* positive is produced here.
#262/#269 stay open; stacked PRs remain Draft / DO NOT MERGE; #208 SDK cutover
is not authorized. The existing negative report's rejection-reason string
remains a test classification after generic UNSUPPORTED, not a reason returned
by the raster API.
