# Native linear and solid cubic Stroke profile

The private scoped renderer V2 expands admitted linear and solid cubic Stroke paths into owned fill
geometry before the native V1 sample/Clip consumer. This advances #213 toward
#208. Public page rendering still uses PDFium; the production runtime, SDK
linkage and final removal gates remain open. No public C11 ABI changes.

## Admitted geometry

Opaque, stroke-only MOVE/LINE/CLOSE paths support multiple segments
and subpaths, positive user-space width up to 2048, butt/square/round caps and
miter/bevel/round joins. Miter limits range from 1 through 1000. A miter beyond its
limit becomes a bevel as required by Stroke semantics. An open subpath stays
open; only an explicit CLOSE creates its closing segment (and the seam join
for solid Stroke).
Consecutive identical vertices collapse; an explicitly repeated first vertex
is removed before handling CLOSE. Collinear forward segments need no wedge.

Each segment creates a rectangle, and each outer join creates a bevel triangle
or miter quadrilateral. Round caps use outward semicircles; round joins use
only the outer circular sector. All pieces have the same orientation, making nonzero
winding their union, including overlaps and self intersections. Stroke does
not use the source fill winding rule. Interior corner overlap is intentional.
Square caps extend only the first/last endpoint of an open subpath.

Widths/caps/joins are constructed in user space. The original affine transform
is preserved on the resulting fill, then the shared FIT device plan is applied
by the sampler. Nonuniform scale, shear, rotation and reflection therefore
transform the entire outline, rather than scaling a device-space width.
Frozen Clip leaves and per-operation q/Q associations use the existing scoped
Clip DAG and shared subpixel membership tests.

## Round sectors and device error

Round caps and joins follow ISO 32000-1, 8.4.3.3–8.4.3.4: caps use an
outward semicircle, while joins connect the two outer stroke edges with a
circular sector. A full disk at a join could spill beyond the butt end of a
short adjacent segment, so the geometry emits only the required sector.
Solid and positive-dash paths share these cap/join producers.

Each circular sector is an inscribed polygon with 2..512 angular segments.
For radius `r`, total angle `a`, and `n` segments, the chord error is at most
`r * a² / (8*n²)` in user space. The Frobenius norm of the complete authored
linear transform followed by the FIT transform bounds magnification into
pixels, including nonuniform scale, shear and reflection. Subdivision doubles
until the resulting chord bound is at most 1/64 pixel. The compiler also
measures every sector vertex's actual double-to-Composer-float rounding error,
requiring its magnified value to be at most 1/128 pixel. This leaves the sector
geometry within 1/32 pixel of its ideal arc; antialiasing remains the existing
4x4 sampler and has its separate source-oracle tolerance.

The planned outline entry point borrows the checked FIT plan solely to choose
arc detail and check precision. The returned fill remains in user space and
must be sampled with the same plan. No plan/provider pointer escapes. The
unplanned entry point uses the authored transform and an identity device map.
An excessive segment count, unrepresentable float precision, singular/nonfinite
transform or insufficient output/edge/sample capacity fails atomically.

## Solid cubic normal strips

Solid MOVE/LINE/CUBIC/CLOSE paths also use the same private V2 entry points.
A regular cubic is subdivided with de Casteljau into convex quadrilateral
normal strips. Adjacent leaves share the same exact tangent cross-section;
there is no authored line join at an artificial subdivision vertex. Joins
between authored line/cubic primitives use their actual endpoint tangents.
Caps likewise use the actual tangent, including the limiting direction for
coincident endpoint controls produced by PDF `v` and `y`. Multiple subpaths,
explicit closing lines and closed cubic loops retain these semantics.

The centerline alone is insufficient to certify a thick Stroke: a small
centerline error can hide a large normal/offset error. For leaf controls
`P0..P3`, the same-parameter centerline deviation from the endpoint chord is
bounded by the maximum norm of
`P1-(2*P0+P3)/3` and `P2-(P0+2*P3)/3` (the difference's Bernstein controls).
The derivative controls `di=P(i+1)-Pi` must lie in a common forward cone about
the chord. Nonzero controls must have strictly positive chord projection;
zero controls are allowed. Thus the derivative cannot vanish in the open
leaf interval. If `T` bounds absolute perpendicular/forward ratios for those
controls and the endpoint unit tangents, the normal interpolation error is
at most `2*T`.

A second-order bound avoids excessive detail where endpoint derivatives are
nonzero. Write `m0=|d0|`, `m2=|d2|`, `U0=d0/m0`, `U2=d2/m2`, and
`K=|d1-(m0*U2+m2*U0)/2|`. The derivative divided by
`(1-t)*m0+t*m2` differs from the linear unit-tangent interpolation by at most
`K/(2*min(m0,m2))`. Its normalization adds at most that error again plus the
interpolated unit vectors' norm deficit, bounded by `|U0-U2|²/4`.
Thus normal error is at most `|U0-U2|²/4+K/min(m0,m2)`, plus measured endpoint
tangent discrepancies from shared subdivision arithmetic. The smaller of
this and the cone bound is used. A monotone collinear cubic is exactly its
line segment as a geometric set, regardless of parameter speed.

For half-width `r`, the center bound plus `r` times the normal bound must map
to at most 1/32 pixel under the combined authored/FIT Frobenius bound. Every
emitted vertex in a curved operation, including caps and joins, also passes
the measured 1/128-pixel float-storage check. Curve strip geometry is thereby
within 5/128 pixel, below 1/16 pixel; sampling/antialiasing remains governed by
its separate source-oracle tolerances. Both the double and stored-float quad
must be strictly convex. Offset folds or insufficient precision/detail refuse
rather than publishing an uncertified polygon. Returned geometry remains
owned in user space and must be sampled with the same borrowed device plan.

Subdivision has maximum depth 16, 65536 visited nodes per operation and at
most 1024 accepted strips/linear segments across all its subpaths. The existing
10240-command, coordinate, actual expanded-edge and global sample-work limits
also apply. Internal stationary points/cusps and exhausted limits refuse
atomically. An operation containing both a cubic and a dash array is still
unsupported: curve arc-length, dash phase and dash endpoint tangents are a
separate qualification contract. No fixed-step or linear fallback is used.

## Dash pattern

V2 also admits up to 64 nonnegative dash lengths, each at most 1000000,
with at least one positive entry, and finite phase in [0,1000000000]. Odd arrays repeat twice before their
on/off parity repeats. The phase is reduced modulo that complete cycle and
reapplied independently at every authored subpath. Pattern distance is measured
in user space before affine/device transforms; a source segment boundary does
not reset it. Empty arrays retain the previous solid/zero-phase contract.

A dash crossing a source vertex keeps the chosen join; a dash ending at that
vertex gets a cap. The closing source segment consumes pattern distance in the
same way as other segments. Each dash is an open fragment: the first and last
dash endpoints get caps even when they coincide at a closed path's seam.
They are not inferred to be connected. This follows the endpoint/join rule in
[ISO 32000-1:2008, 8.4.3.6](https://opensource.adobe.com/dc-acrobat-sdk-docs/standards/pdfstandards/pdf/PDF32000_2008.pdf)
and is checked with independent solid/empty corner probes and direct original
PDFium renders, including a dash ending exactly at the seam.

Zero on entries are directed dash fragments, rather than discarded distances.
Their start/end caps form a circle for round caps or an oriented width-by-width
square for projecting caps; butt caps have zero coverage. A zero gap still
separates adjacent dashes, so each retains its caps instead of creating a join.
Consecutive zero entries retain their on/off parity, including across an odd
array's second copy. Exact phase boundaries preserve zero on entries at the
start of the traversed interval. Every zero transition counts against the
shared work cap, even for invisible butt dots. All-zero arrays refuse before
modulo arithmetic or allocation.

At an internal vertex a new zero dash takes the outgoing segment's tangent.
The traversed interval is half-open: a new dot at the terminal path distance
is not synthesized. A positive dash already reaching that endpoint still gets
its normal end cap. The same rule avoids a duplicate dot at a closed seam.
Independent Poppler and direct pinned PDFium probes confirm terminal omission;
provider-free goldens verify the exact native positions and outgoing square
orientation. Degenerate authored subpaths remain a separate refusal contract.

An otherwise admitted path entirely inside an off interval succeeds with empty
coverage. A cancelled two-edge nonzero contour represents this exact empty
fill, so normal transform, Clip, budget and later-operation validation still
runs. This is not a shortcut around page admission.

## Exact explicit closed return — qualified subset

A solid authored `MOVE A → LINE B → LINE A → CLOSE` is admitted **only**
when the final distinct `LINE A` returns exactly to the initial point and
is followed immediately by CLOSE. It has the same centered solid Stroke
coverage as `A→B→CLOSE`, with the same Bevel/Round join constraint below.
The private line compiler normalizes the exact final return only for this
pattern; the mixed cubic compiler avoids emitting a duplicate reversing
segment when another subpath selects the cubic backend. No source command
storage or provider ownership is modified.

Open `A→B→A`, Miter joins, dashed return/cusps, and any additional segment
after the return remain atomic `QUANTAPDF_ERROR_UNSUPPORTED`. Strict C11
byte-equivalence, original-source/PDFium fixtures, and tiny-budget negatives
are required without broadening the renderer or changing sample tolerances.

## Closed two-point reversal — qualified subset

An authored solid `MOVE A → LINE B → CLOSE` with **two distinct finite
points** traces a segment forward and back. Its two 180-degree join regions
are admitted only for **Bevel** and **Round** joins, with bounded user-space
stroke width:

- Bevel joins yield the original butt-ended strip without cap extensions
- Round joins yield that strip plus outward semicircles at A and B, produced
  with the existing error-bounded arc sectors
- Caps are not consulted: this is an explicitly closed path, not an open
  path with two caps

The same subset is admitted inside an otherwise solid cubic-containing
operation. Unknown miter cusp semantics, open authored reversals and other cusps,
dash arrays, zero width/hairlines, and arbitrary stationary curves remain
atomic `QUANTAPDF_ERROR_UNSUPPORTED` cases. Output geometry is still
checked under affine/FIT transforms, actual expanded-edge and sample budgets,
Clip and the same final owned pixel publication. The independent original-PDF
oracle uses unchanged PDFium thresholds; pure C11 tests also require
byte-identical equivalence to the corresponding open Butt/Round-cap strip.

## Closed single-point degenerate Stroke

ISO 32000-1 §8.5.3.2 explicitly admits a *single-point closed subpath*
(including a closed sequence of coincident LINE endpoints) as a zero-length
stroke. For **solid positive-width Stroke**, the existing bounded normal
round-cap geometry produces a full disk. Butt and projecting-square line caps
produce exact empty coverage. The same rule is applied inside mixed linear and
solid-cubic operations. No new rasterizer or approximation is introduced.
The source-PDF path lowerer now preserves MOVE/CLOSE-only geometry for
Stroke (including the implicit CLOSE in the `s` operator). It continues to
reject all-segment-free Fill and Clip, whose device-dependent semantics are
not covered by this native Stroke increment.

A **MOVE-only open** subpath remains inert, even with Round Cap.
Closed two-point reversals now have the separate restricted join profile
above; other authored return/cusp patterns and dashed zero-length closed
paths remain explicit `QUANTAPDF_ERROR_UNSUPPORTED` cases.
Nonfinite transforms, insufficient expanded-edge budgets and later invalid
operations fail atomically as before. All pixel tests use the established 4×4
sampling and independent PDFium source-oracle tolerances.

## MOVE-only open subpaths

A single MOVE, or a MOVE command without an authored LINE or CUBIC
segment, contributes no stroked geometry, regardless of cap style or a
valid dash/phase array. A lone LINE/CLOSE or non-finite MOVE remains refused.
Consecutive MOVE commands and an unterminated trailing MOVE therefore form
empty subpaths, and can precede or follow admitted linear or solid cubic
subpaths without changing their output.

The empty result continues through the ordinary checked transform, clipping,
resource, edge/sample-budget and atomic-output pipeline. A no-geometry
operation still uses the existing two-opposite-edge zero-winding encoding,
not a short circuit into success or a fallback renderer. Closed single-point subpaths are qualified separately below; zero-width
hairlines and genuinely degenerate cubic curves remain unqualified. All non-finite coordinates and unsupported subsequent path
commands still refuse the entire operation. No public ABI or renderer
selection changes.

## Zero-length open subpaths

A subpath containing MOVE followed by one or more LINE commands at the same
point is admitted for solid, positive-width Stroke. Round caps emit a bounded
full disk. Butt and projecting-square caps produce exact empty coverage, as
required for a degenerate PDF stroked subpath whose cap orientation is
indeterminate (ISO 32000-1, 8.5.3.2). Round geometry uses the existing
device-error-bounded arc producer. The same rule applies to a degenerate
open line subpath in a mixed line/cubic operation. Closed single-point
subpaths now share the solid-cap contract above; two-point closed
returns/cusps, dashed zero-length paths and zero-width hairlines remain explicit
refusals until their separate contracts are qualified.

The pinned PDFium build emits a small point for degenerate butt/square cases.
Those fixtures retain the fixed aggregate mean/gross comparison but do not
assert the provider-specific center pixel; the native numerical goldens follow
the PDF specification rather than copying that behavior.

## Dashed cubic admission boundary

Dashed cubics are admitted only when every cubic segment is an exact monotone
collinear trace. Its two control points must lie exactly on the endpoint line
and occur in nondecreasing order along that line (in either direction). Such
a cubic has the same geometric trace and arc length as its endpoint segment,
so it is lowered to that segment and reuses the already-qualified user-space
dash/phase logic. This also supports mixed line/collinear-cubic paths without
changing dash distance at the authored segment boundary.

Non-collinear curves and collinear reversing curves remain explicit
`QUANTAPDF_ERROR_UNSUPPORTED` refusals. No fixed-step curve flattening,
parameter-distance approximation, retry or alternate renderer is used.
General curved arc-length/phase and curve dash endpoint tangents remain open
work under #213.

## Bounds, ownership and refusal

The outline compiler accepts at most 1024 authored commands. Its fixed maximum
is 10240 output commands, with coordinates bounded to +/-1000000 before and
after expansion. It checks nonzero polygon area after conversion to Composer
float storage. Every emitted edge counts against the caller's edge budget.
Dash phase traversal, zero transitions and segment splitting share a 65536-step
cap per operation;
non-progressing arithmetic refuses rather than looping or dropping a dash.
The scoped V2 renderer builds bounded outlines before partitioning the global
budgets, using their actual expanded edge counts as Stroke weights. Clip
weights retain their authored command bounds. The sampler preflights actual
outline/Clip edge and sample work against each share before publishing a layer.
A dense dash pattern cannot be billed as its short authored source path.

The input IR, options, commands and device plan are borrowed synchronously
and remain unchanged. At most four preflight outlines each own a bounded
command array; they remain alive while layer sampling borrows them, and all
are disposed on success or any failure. No source, Graph, qpdf, PDFium or
provider state is retained. A late unsupported layer discards
the complete final image. Every failure resets the caller's image output.

Hairlines/zero width, dashed curves, alpha,
paint resources, exact reversal/cusps, move-only/all-zero subpaths, closed
linear subpaths with fewer than three distinct vertices, invalid coordinates and
exhausted budgets remain explicit refusals. This profile requires MOVE after
CLOSE to begin another subpath. Fill+Stroke combined operations remain outside
this profile. No provider fallback or best-effort partial painting exists.

V1 and its pinned M2 rectangular Stroke admission remain unchanged. V2 is an
explicit private entry point, rather than a retry after V1 rejection. In
particular, the original jsPDF repeated-close tail is still qualified through
V1/M2; it is not claimed as a V2 positive.

## Evidence

`quantapdf.native_stroke_polyline_v1` compiles as strict pure C11 without qpdf
or PDFium linkage. Numerical goldens cover miter/bevel/limit behavior, caps,
explicit closing, overlapping contour union, transformed widths, FIT scaling,
round-sector coverage, short-segment spill prevention, measured float chord
error, device-dependent subdivision, precision/segment-limit refusals,
detached outline lifetime, immutable input, malformed/budget refusals and
late-operation atomic rejection. Dash goldens cover odd repetition, modulo
phase equivalence, on/off probes, restart at MOVE, crossing/ending at a vertex,
closed-seam caps, all-off output, maximum array length, exact expanded-edge
budgets, expansion exhaustion and immutable dash arrays.

`quantapdf.native_stroke_polyline_oracle` generates 30 original source PDFs
with three content streams, captures Graph, lowers owned IR and destroys all
source/Graph ownership before V2 rendering. It compares the resulting detached
pixels with direct pinned PDFium C API renders of the original source files,
never IR re-export. Cases cover open/closed paths, multiple overlapping
subpaths, caps/joins, acute miter, nonuniform scale/shear/reflection/rotation,
q/Q restoration and nested W/W* Clip. Eighteen cases exercise dashes, including
phase/odd arrays, closed seams, affine transforms, mixed layer budgets and
q/Q pattern restoration. It also checks five source negatives.
Per fixture, mean absolute channel error must stay below 2/255, fewer than 1%
of pixels may have a channel error above 64, and explicit solid/empty/color
probes must match within 3/255. Different edge antialiasing is allowed.

`quantapdf.native_round_stroke_oracle` runs a separate mode of the same source
oracle with process/fixture-specific generated filenames, allowing parallel
CTest runs without sharing a source path. Exact original bytes from QPDFWriter
are checked after writing, Graph lowering and direct PDFium rendering; negative
cases also verify unchanged source bytes before cleanup.
Its 22 positives cover horizontal/vertical/diagonal caps, both turn directions,
acute/closed/short joins, subpaths, positive dashes and seams, affine/reflected
transforms, nested W/W* Clip, q/Q restore, all-off and thin strokes, plus two
144-DPI FIT comparisons with direct PDFium renders on the same 240x240 grid.
Three additional source negatives exercise hairline, dashed-cubic and dense
zero-dash expansion refusals. The mean/gross/probe thresholds are unchanged. Source and Graph are
destroyed before native rasterization, and detached pixels survive IR disposal.

`quantapdf.native_zero_dash_oracle` adds 22 generated original-source positives
under the same mean/gross/probe thresholds, including round/square/butt dots,
phase/wrapping, odd and consecutive-zero arrays, zero gaps, oblique vertices,
closed seams, subpath/state reset, affine transforms, W/W* Clip and 144-DPI FIT.
Three negatives cover dense visible/invisible transition exhaustion and cusps.
Pure C11 goldens additionally verify exact square/empty coverage, odd parity,
large modulo-equivalent phases, maximum arrays, exact expanded edge limits,
immutable dash data, detached outline lifetime and atomic late-layer refusal.

### Zero-interval oracle limitation

An additional immutable original-source pair uses `[0 16]` with phases 5 and
16005. Native output must be byte-identical, with independent solid probes at
its mathematically expected dot centers. Direct PDFium outputs are measured
and logged separately; this pair is not counted as a raster-oracle positive.
The pinned Linux PDFium reference shifts the large-phase dots and leaves small
butt-dot coverage. Published PDFium AGG code replaces tiny dash lengths with
0.1 before applying phase, consistent with this observation (the cause in the
pinned binary is an inference):
[RasterizeStroke at 21c5df09](https://pdfium.googlesource.com/pdfium/+/21c5df09b20fbb1b8023337729ac9bf21807207c/core/fxge/agg/cfx_agg_devicedriver.cpp).
The native compiler preserves exact zeros and mathematical period; it neither
copies that adjustment nor weakens the existing oracle pixel thresholds.
The two original files retain their unreduced authored phases and byte guards.

`quantapdf.native_cubic_stroke_oracle` adds 24 generated source positives and
four negatives. Cases include endpoint caps, smooth primitives under all
join styles, inflections, reversing control polygons, authored line/cubic
joins, closed four-cubic circles and single-cubic loops, subpaths, `v`/`y`,
nonuniform/reflected transforms, q/Q and W/W* Clip, wide strokes and 144-DPI FIT.
The new cubic fixtures explicitly budget up to 4096 expanded edges and
800000000 sample tests (four times that sample/pixel budget at 144 DPI).
Existing modes and pixel thresholds are unchanged. Source/Graph are destroyed
before native rendering and the original bytes are guarded through direct
PDFium comparison, as in the other source modes.

Pure C11 goldens independently evaluate an analytic arch and its two true
normal-offset boundaries at 2000 parameter positions, measuring distances to
the emitted float strip sides at 72/144 DPI. They also verify true tangent
endpoints, split equivalence, join-style invariance inside a primitive,
collinear speed equivalence, endpoint-control limits, exact expanded edge
capacity, immutable input, detached lifetime and atomic late/refusal behavior.

These are generated feature fixtures. They add no independently published
PDFs or formal external W* positives to the existing four-document corpus.
