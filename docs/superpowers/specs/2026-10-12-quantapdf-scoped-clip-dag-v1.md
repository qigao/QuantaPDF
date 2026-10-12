# Native scoped Clip DAG V1 — multi-operation bounded ownership

Tracks [#214](https://github.com/qigao/QuantaPDF/issues/214) and the
PDFium-free roadmap [#208](https://github.com/qigao/QuantaPDF/issues/208).

## Scope

This **private C11** entry point composes 2–4 ordered opaque solid PATH
operations on one page. Each operation may borrow its own normal/full-alpha
graphics state referring to an arbitrary bounded Clip Path/Intersection DAG,
or may have no graphics state/Clip. The same Clip leaf can be reached both
through nested intersections and directly from another operation; Clip
scope is determined by the current operation's selected resource ID, not by
a global mutable state stack.

The implementation reuses native Raster V1 sample-wise clip intersections,
and the native Clip DAG V3 tri-color DFS and unique-leaf admission.
Every root can have up to four unique leaves, eight registered nodes and
maximum depth four. Shared graph nodes are allowed, cycles, duplicate
siblings, unowned graph nodes, missing IDs, nonfinite/singular leaf CTMs,
unsupported resources or alpha/blend fail closed. The union of all operation
root traversals must cover all registered Clip resources. The renderer
preserves the order of fills exactly and does not let a prior Clip restrict
a later operation.

## Ownership, bounded work and publication

The source `quantapdf_ir`, paths and Clip nodes remain borrowed for the
entire synchronous render call. The operation-scoped IR views are stack-owned
and never escape. Each paint produces at most one temporary detached
transparent RGBA8 bitmap. It is source-over composited into a single
private output buffer; the final bitmap is published **only after every
operation succeeds**.

The per-request maximum edge capacity **and** sample-intersection budget
are conservatively divided between operations, enforcing an aggregate
upper bound. Page and pixel limits, per-op cubic flattening depth, graphics
state scope and resource ownership are checked; no PDFium/qpdf production
linkage, automatic fallback, persistence or retry is introduced. The old
`quantapdf_ir_raster_scoped_operations_v0` API is unchanged.

## Conformance

`quantapdf.native_scoped_clip_dag_v1` is an isolated strict C11 executable:
test and native source units only, **not** QuantaPDF runtime, qpdf or
PDFium. Cases cover the order of three paints (nested red, leaf-shared
blue, unclipped green), reverse paint order, fractional Clip translation,
transparent page, identical rerenders and failure atomicity when the
last operation is unsupported. Bad states, cyclic DAGs, singular CTM,
unowned resources and sample/edge budget exhaustion must fail.

A separate test-only QPDF Composer → PDFium differential pixel oracle is
required before treating this subset as fully pixel-qualified; it must
compare the **same canonical IR**, including state restoration across
operations, and keep fixed published tolerances.

## Explicit limitations

This is an IR-level, per-operation scoped renderer—not yet an interpreter
for arbitrary PDF `q/Q` streams. It does not cover more than four paint
operations, arbitrary nested graphics groups, soft masks/transparency,
stroke+clip, text/image/form rendering or a PDFium-free public SDK.
#214 and #208 stay open until their broader gates are satisfied.
