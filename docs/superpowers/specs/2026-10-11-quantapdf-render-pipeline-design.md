# Provider-independent render planning

Implementation decision for [#154](https://github.com/qigao/QuantaPDF/issues/154),
following the private backend boundary and Composer IR. This increment moves
raster planning into a pure-C module consumed by the existing PDFium renderer.
Graph-to-IR interpretation, direct IR rendering, additional providers and parallel
SDK calls remain required follow-up; this is not completion of those features.

## Evidence and decision

MED, fact: `src/backend/pdfium_document.cpp` currently mixes source-page geometry,
DPI/clip/rotation planning, integer bitmap dimensions, PDFium drawing and RGB(A)
publication in one operation. `src/document/render.c` admits public option tails
and retains the bitmap's context, while all raster geometry remains provider-local.
The existing render, page-box, transform, form and image tests depend on this
path. Other backends cannot reuse its planning without importing PDFium types.

HIGH, fact: several float-to-int conversions occur after rounding, without
checking the rounded result; aligned draw offsets and full-page draw dimensions
also lack representability checks. A plan must validate every final integer and
float conversion before any raster allocation or drawing.

HIGH, fact: a new asymmetric color-pattern regression fails on the baseline as
well as the initial extraction of this code. The matrix path reapplies a raw
page-to-display transform although
[PDFium chromium/8021 already applies its display matrix](https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8021/fpdfsdk/fpdf_view.cpp#836)
before concatenating the caller's matrix. Rotation therefore mirrors or
misplaces content. The adapter must supply only its additional `/UserUnit`
scale before the requested clip/rotation/DPI transform. CropBox offset, intrinsic
rotation and the y-axis flip already belong to PDFium's display matrix.

Add private `src/render/render_plan.h/.c`. The pure planner accepts the displayed
page extent and already-admitted rendering options. It produces an immutable
value containing pixel dimensions, tightly packed RGB(A) stride/size, background,
page-to-device transform and, when applicable, the aligned draw rectangle.
Source coordinates are deliberately separate: a backend supplies its remaining
source-to-displayed-page transform to a checked composition helper. PDFium's
matrix renderer takes already displayed coordinates, with `/UserUnit` supplied
by our adapter. IR already in displayed page points can use identity here.

The provider continues to resolve page geometry and `/UserUnit`, acquire its
process guard, draw content/annotations/forms and convert its native pixel
format. The document facade retains its existing bitmap/context publication and
failure cleanup. No page/model owner moves into the plan. The same private
archive carries the C implementation, preserving the installed static link
closure without a new public library or dependency.

## Geometry and compatibility

Displayed page space follows the existing CropBox origin, downward y axis and
point units. Clip coordinates select a region in that space, including allowed
regions outside the page. Rotation applies to the selected region after DPI
scaling. Compute its four transformed corners, then floor the minimum and ceil
the maximum for integer raster coverage. Preserve the existing near-zero sine
and cosine threshold and arithmetic order for ordinary inputs; no pixel or
cross-platform bitwise determinism beyond the existing provider is claimed.

Check rounded dimensions, aligned offsets and full-page draw sizes against
`int` before conversion. Check packed stride against the existing `int` contract
and total bytes against `SIZE_MAX`. A final provider matrix must be finite,
representable as float and nonsingular after conversion. These are representation
checks, not new arbitrary page
or memory budgets. Raster/provider allocation can still fail with NOMEM.

Keep the existing exact unrotated execution policy: the integer draw rectangle
feeds PDFium's ordinary renderer and its separate form draw. Other rotations
feed the matrix renderer. This preserves widget behavior and pixel rounding;
the matrix path's existing form-draw limitations are not changed or concealed.
An out-of-range aligned rectangle fails instead of switching rendering paths.

## Ownership, failure and concurrency

Planning allocates nothing, stores no borrowed pointers and has fixed bounded
work. Inputs are borrowed for one call; successful publication copies a complete
value. Copies of a completed plan are independent. Output resets before failure;
invalid options or unrepresentable requested output return ARGUMENT, malformed
source extent/transform FORMAT. The composition helper requires a completed,
unmodified plan; an empty plan returns STATE. Inputs and outputs must not alias.

Provider matrix composition and all planning finish before raster allocation.
Failed planning leaves the public bitmap output empty; existing provider and
context cleanup still apply. Plans do not retain a document, context, graph or
IR. The caller must keep the selected source live for the actual render. Pure
planning on independent values needs no shared state; ABI v2's external
serialization, provider guards and destruction rules remain in effect. This
prepares independent per-page requests without enabling concurrent SDK calls.

## Alternatives, migration and validation

Moving provider handles into a generic job object would preserve the coupling.
Rewriting rendering through serialized IR would combine graph interpretation,
resource semantics and raster changes before they have compatibility evidence.
Choose the reusable planning boundary now and keep the concrete public render
consumer. Later graph/IR consumers must supply their own admitted source geometry
and lifetimes; they may not reuse stale source-page geometry after an edit.

No public ABI, export, option layout, persisted format or package target changes.
Reverting restores provider-local planning without document migration, including
the former matrix-path defect. Aligned pixel results remain stable; rotated
content now follows displayed-page rotation instead of the repeated flip/offset.
Inputs outside the target numeric ranges fail before conversion/allocation.

Formal CTest coverage includes a backend-free planning suite with analytical
geometry, clip/rotation coverage, identity/source-transform composition, copied
plans, error reset, and numeric bounds. Existing public rendering tests verify
actual dimensions, pixel data, alpha, option tails and thumbnail policy. A
Composer-produced asymmetric color pattern checks positive, negative and oblique
rotation, DPI, clipping, offset CropBox and byte-identical repeated renders.
Distinct painted quadrants verify requested rotation relative to pages with
intrinsic rotation and `/UserUnit`. Adjacent
page geometry, transforms and interactive suites cover provider behavior. Run
focused tests, complete native Release/sanitizer suites and normal SDK installs.
Linux static CI verifies archive closure and its fatal ASan/UBSan configuration;
report separately any platform not run.
