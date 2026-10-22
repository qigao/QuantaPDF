# Native linear Stroke profile

The private scoped renderer V2 expands straight Stroke paths into owned fill
geometry before the native V1 sample/Clip consumer. This advances #213 toward
#208. Public page rendering still uses PDFium; the production runtime, SDK
linkage and final removal gates remain open. No public C11 ABI changes.

## Admitted geometry

Opaque, stroke-only MOVE/LINE/CLOSE paths support multiple segments
and subpaths, positive user-space width up to 2048, butt/square caps and
miter/bevel joins. Miter limits range from 1 through 1000. A miter beyond its
limit becomes a bevel as required by Stroke semantics. An open subpath stays
open; only an explicit CLOSE creates its closing segment (and the seam join
for solid Stroke).
Consecutive identical vertices collapse; an explicitly repeated first vertex
is removed before handling CLOSE. Collinear forward segments need no wedge.

Each segment creates a rectangle, and each outer join creates a bevel triangle
or miter quadrilateral. All pieces have the same orientation, making nonzero
winding their union, including overlaps and self intersections. Stroke does
not use the source fill winding rule. Interior corner overlap is intentional.
Square caps extend only the first/last endpoint of an open subpath.

Widths/caps/joins are constructed in user space. The original affine transform
is preserved on the resulting fill, then the shared FIT device plan is applied
by the sampler. Nonuniform scale, shear, rotation and reflection therefore
transform the entire outline, rather than scaling a device-space width.
Frozen Clip leaves and per-operation q/Q associations use the existing scoped
Clip DAG and shared subpixel membership tests.

## Dash pattern

V2 also admits up to 64 strictly positive dash lengths, each at most 1000000,
and finite phase in [0,1000000000]. Odd arrays repeat twice before their
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

An otherwise admitted path entirely inside an off interval succeeds with empty
coverage. A cancelled two-edge nonzero contour represents this exact empty
fill, so normal transform, Clip, budget and later-operation validation still
runs. This is not a shortcut around page admission.

## Bounds, ownership and refusal

The outline compiler accepts at most 1024 authored commands. Its fixed maximum
is 10240 output commands, with coordinates bounded to +/-1000000 before and
after expansion. It checks nonzero polygon area after conversion to Composer
float storage. Every emitted edge counts against the caller's edge budget.
Dash phase traversal and segment splitting share a 65536-step cap per operation;
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

Hairlines/zero width, round caps/joins, curves, zero dash entries, alpha,
paint resources, exact reversal/cusps, move-only/all-zero subpaths, closed
subpaths with fewer than three distinct vertices, invalid coordinates and
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

These are generated feature fixtures. They add no independently published
PDFs or formal external W* positives to the existing four-document corpus.
