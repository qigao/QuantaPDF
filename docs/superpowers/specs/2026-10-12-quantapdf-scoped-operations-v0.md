# Native Scoped Path Operations V0 — bounded per-operation Clip

Implements a narrowly admitted stage of [#214](https://github.com/qigao/QuantaPDF/issues/214)
under PDFium-free roadmap #208. This is a **private, pure C11** renderer,
not a replacement for the public PDFium-backed renderer.

## Semantics

The IR contains exactly one page and **2–4 ordered PATH operations**.
Each operation independently selects either no graphics-state resource,
a Normal/full-alpha graphics-state with no Clip, or a Normal/full-alpha
graphics-state pointing directly to one canonical Clip Path resource.
The same graphics-state and Clip IDs can be borrowed across operations.
Only opaque solid fills (MOVE/LINE/CUBIC/CLOSE, Even-Odd/Nonzero,
nonsingular finite transforms) are admissible.

The implementation creates a **single-operation immutable projection** for
each source operation, including a transparent local page. It reuses the
existing V1 native 4×4 sample renderer, with per-sample Clip winding where
selected. Each detached layer is composited in original operation order with
checked straight-alpha source-over integer arithmetic. The first paint's
Clip cannot restrict the second paint: every operation is evaluated against
its own resource IDs and the local Clip. The page background is drawn only
once in the final result.

For this restricted opaque normal-blend subset, scoped per-operation
graphics-state reuse models Composer's q/Q restoration around operations.
It does **not** interpret arbitrary q/Q instruction streams, group blend
isolation or PDF content state transitions.

## Ownership, failure, budgets

The original IR and every reachable source record are borrowed and
unchanged. Intermediate per-operation buffers are detached and released
within the synchronous call. The final image is published only after **all**
operations succeed; an unsupported late operation releases all intermediate
work and returns an empty output slot.

Explicit bounds: at most four operations, states and Clip Path resources;
one page; full resource validation (all state/Clip resources must be used);
pixel and edge budgets provided by the caller; V1 sample-work budget divided
across operations so the aggregate never exceeds the caller's request.
At most the final bitmap and one isolated operation bitmap coexist.
No hidden fallback to PDFium/qpdf or unbounded retry.

## Tests and unfinished features

Standalone `quantapdf.native_scoped_operations_v0` compiles only C11
native source files (no QuantaPDF runtime, qpdf or PDFium links). It verifies
two differently clipped paints and an un-clipped later paint, overlap order,
deterministic repeated render, transparent background, fractional Clip
boundary, unchanged ownership and rejection on a **late unsupported operation**
without publishing partial pixels. Also covers invalid resource IDs,
unsupported Clip kind, alpha, transform, operation limits and sample budget.

Still unsupported: nested Clip intersection DAG, multiple pages in a single
call, more than four draw operations, stroke+clip, alpha/blend/transparency
groups, arbitrary PDF graphics-state parsing, Form resources, native text and
images. Add a separately built **test-only QPDF/PDFium differential pixel
oracle** for this multi-operation scope before calling it pixel-qualified.
#214, #208, and #154 remain open until their full acceptance is met.
