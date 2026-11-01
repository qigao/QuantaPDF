# Owned native geometry and page-wide compilation — M1b.2b

This increment of [#570](https://github.com/qigao/QuantaPDF/issues/570) separates
the existing V1 geometry compiler from its existing C11 4x4 sampler. The
compiler returns a private unique owner; execution accepts that owner rather
than IR, Clip records or a caller's device plan. No parser, provider, pixel
algorithm, public ABI or Source admission is added.

## Retained geometry

The owner retains bounded flattened edges, draw/Clip descriptors, coverage
envelopes, optional candidate-edge boxes, frozen styles/background and an owned
device-plan value. Ordinary PATH/Clip execution has no Source-command borrow.
Text/exact-cubic classification retains checked copies of the original canonical
commands, including Clip commands. Classification still performs its original
interval/ray/distance work at execution time; masks or pixels are never cached.
No new flattening tolerance, miter policy, coordinate stretch or numeric quota
is introduced. Physical viewport fringes and original operation order remain.

The header, draw records and command snapshots share the previous draw-array
malloc. The edge allocation remains first; exact coverage scratch and output
remain third/fourth. The original four exact-raster malloc hooks/counts and
failure statuses are preserved. The checked requested-byte envelope now includes
the actual owner header/record sizes, at most four simultaneous geometry owners
and the already bounded canonical-command inventory. This accounts for the new
retained representation without changing pixel, edge or sample-work caps.

## Lifecycle and quota

The handle is zero-initialized, unique and non-copyable. Its synchronous ledger
borrow must outlive it and cannot be reinitialized while the owner is live.
Preparation reserves the same child sample/edge allowance before geometry
allocations and publishes no owner on failure. It allocates no pixel buffer.
First execution settles that lease with the unchanged billed work and actual
compiled edge count. Later executions reserve fresh sample allowance from the
same page ledger and do not rebuild or rebill retained edges. Historical fixed
child allowances remain conservative, even when a smaller actual bill is known.

Compiled payload is immutable across execution. Only the handle's lease/lifecycle
state changes. Each successful execution allocates independent detached pixels;
those remain valid after geometry disposal. A failed execution clears its output
and measured work, aborts its lease and cannot retry through a failed page ledger.
Disposal frees every retained geometry buffer. Abandoning an unexecuted owner
aborts its pending page lease; normal disposal after successful execution returns
geometry byte credit without returning already billed work. Empty disposal is
idempotent. Scratch releases remain valid during failed-page cleanup.

## V5 page preparation

After the existing complete resource/state/pixel preflight, V5 prepares all
selected PATH and Image-mask owners before allocating any layer pixels. Each
owner retains the existing equal child share; simultaneous reservations fit the
same page cap. An unsupported late primitive or failed compilation cannot start
an earlier layer's sampler. Owners are consumed in original paint order and
released after their layer, with unconditional cleanup of the remaining owners.

Image masks compile the same original rectangle/Clip intersection. The existing
integer one-leaf profile retains its exact Clip inputs; fractional and explicit
multi-leaf profiles retain their combined viewport/Clip predicate. RAW samples
remain immutable parent-owned data under the complete Source IR lifetime, and
their Source/placement pixel inventory is still charged once by V5. No profile
is retried after a failed compile or execute.

## Acceptance

The existing C11 device executable destroys temporary PATH/Clip commands,
operation/page records, limits and plans after compilation; it then checks
independent pixels, two replays under one aggregate quota, refusal of a third,
single edge billing, unchanged geometry info, output survival after owner free,
abandonment, byte-limit failure/reset and exact Text's physical 96-DPI edge.
The two-layer V5 test now verifies both reservations are held before sampling;
an insufficient second reservation produces zero completed samplers.

The real Source/OOM executable reuses one existing positive N-work execution
per Text/original-TeXample DPI to compile from temporary views of actual Source
IR, overwrite every temporary command/descriptor/device plan, and then compare
the full original independent RGBA and work bill. No extra expensive original
render is added. Remaining original positives, N/N-1/edge checks and all four
malloc-failure/recovery paths still exercise the production bridge. No reference
image, original PDF, workflow, CMake target or consumer/installer project changes.

## Remaining M1 work

**M1 remains open.** The outer Source metadata plan is still a synchronous
complete-IR borrow. V5 owns its prepared primitive collection within each
execution; the metadata object does not retain a compiled page or adopt Source
IR between calls. A common outer page owner/admission representation remains
before the planned M2 composition expansion. Original TeXample 72/96/144-DPI
and yWorks 72-DPI evidence and corpus counts are unchanged. Exact HEAD/tree and
actual platform CI results are recorded in the PR and #570.
