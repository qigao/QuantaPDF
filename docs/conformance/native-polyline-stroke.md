# Native linear Stroke profile

The private scoped renderer V2 expands straight Stroke paths into owned fill
geometry before the native V1 sample/Clip consumer. This advances #213 toward
#208. Public page rendering still uses PDFium; the production runtime, SDK
linkage and final removal gates remain open. No public C11 ABI changes.

## Admitted geometry

Opaque, stroke-only, solid MOVE/LINE/CLOSE paths support multiple segments
and subpaths, positive user-space width up to 2048, butt/square caps and
miter/bevel joins. Miter limits range from 1 through 1000. A miter beyond its
limit becomes a bevel as required by Stroke semantics. An open subpath stays
open; only an explicit CLOSE creates its closing segment and join.
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

## Bounds, ownership and refusal

The outline compiler accepts at most 1024 authored commands. Its fixed maximum
is 10240 output commands, with coordinates bounded to +/-1000000 before and
after expansion. It checks nonzero polygon area after conversion to Composer
float storage. Every emitted edge counts against the caller's edge budget.
The scoped renderer conservatively weights Stroke at eight edges per authored
command when partitioning the global edge/sample budgets across operations
and their Clip leaves. The existing sampler preflights all actual expanded
edge/sample work against each share before publishing a layer.

The input IR, options, commands and device plan are borrowed synchronously
and remain unchanged. The temporary outline owns its command array, is used
only during that operation, and is disposed after sampling. No source, Graph,
qpdf, PDFium or provider state is retained. A late unsupported layer discards
the complete final image. Every failure resets the caller's image output.

Hairlines/zero width, round caps/joins, curves, dashes/nonzero phase, alpha,
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
late-operation atomic rejection.

`quantapdf.native_stroke_polyline_oracle` generates 12 original source PDFs
with three content streams, captures Graph, lowers owned IR and destroys all
source/Graph ownership before V2 rendering. It compares the resulting detached
pixels with direct pinned PDFium C API renders of the original source files,
never IR re-export. Cases cover open/closed paths, multiple overlapping
subpaths, caps/joins, acute miter, nonuniform scale/shear/reflection/rotation,
q/Q restoration and nested W/W* Clip. It also checks five source negatives.
Per fixture, mean absolute channel error must stay below 2/255, fewer than 1%
of pixels may have a channel error above 64, and explicit solid/empty/color
probes must match within 3/255. Different edge antialiasing is allowed.

These are generated feature fixtures. They add no independently published
PDFs or formal external W* positives to the existing four-document corpus.
