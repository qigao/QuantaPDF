# Owned Source page and retained compilation — M1b.2c

This increment of [#570](https://github.com/qigao/QuantaPDF/issues/570) gives the
private Source bridge an address-stable, unique prepared-page owner. The page
contains committed Source IR, a frozen metadata copy with no IR pointer, a
stable page ledger, and either the existing exact Text/cubic geometry owner or
the compiled V5 layer collection. It is zero-initialized and must never be copied
or moved while live. There is one QPDF interpreter and one existing C11 painter.

## Commit and disposal

Source admission still destroys every QPDF owner before native preparation.
Preparation first runs the same resource closure, route/device predicates and
geometry admission. It allocates no layer pixels. Only after every selected
geometry is compiled does commit move the complete owned IR into the page and
clear the caller's IR. A preparation failure publishes an empty page, releases
all partial compilation, and leaves the input IR and its ownership untouched.
Disposal releases pending leases/compiled geometry before disposing adopted IR;
empty disposal is idempotent. The Source bridge uses this lifecycle on success
and every failure path. No new heap allocation is needed for the inline owner.

The metadata-only preparation API remains an allocation-free synchronous borrow
for existing private callers. Its old one-shot executor now compiles, executes
and disposes the same common page implementation. It does not create another
renderer or enable a rejected profile.

## Retained projection and V5 execution

The existing device projector feeds the V5 compiler synchronously. Its temporary
PATH/Clip commands, GS graph, device plan and IR records do not escape. The V5
collection retains compiled geometry and copies page/background values, image
placements and normalized RAW descriptors. RAW sample/alpha storage is borrowed
from the enclosing adopted IR, which outlives every layer. Integer clipped RAW,
fractional RAW and unmasked RAW retain their original independent pixel
algorithms and allocation order. The integer helper's pixel producer is shared
with the old validating entry; prepared execution never reads Clip IR.

V5 execution only reserves replay work, samples retained geometry, paints RAW
layers and composites in original order. It does not reconstruct device
projection, resolve Clip/GS graphs or compile another primitive. All selected
fixed child sample shares are held before the first pixel. The collection stays
live across execution and is released by its enclosing owner, including failed
or abandoned preparation. Exact classification still runs and is billed during
sampling; no mask or pixel cache is added.

## Accounting and publication

One compiled page lifetime owns one ledger. Source pixels are inventoried once.
Every successful replay charges its image placements again; each geometry replay
reserves its historical fixed sample share, bills fresh actual work and does not
rebuild/rebill retained edges. Conservative full-page child shares can refuse a
second execution even when its actual bill would be smaller. No per-layer,
per-mask or replay reset is permitted. Failure poisons this page; a new admitted
page lifetime is needed for recovery. Receipts are cumulative for retained pages;
the compatibility one-shot wrapper preserves the old per-call receipts.

The requested-byte ledger covers adopted Source payloads, retained geometry and
all private scratch/layer/output buffers. Successful image publication explicitly
transfers output byte credit to the external image owner. Published images are
independent, use ordinary image disposal, survive page disposal and are outside
the private page's subsequent live-byte count. This matches the existing boundary
excluding later public Bitmap/Context storage. Allocator overhead, stack records
and QPDF parser internals remain outside this requested-payload measurement.
The existing actual-type geometry plus six-RGBA-surface envelope still bounds
all retained geometry and private pixels; no numeric cap is increased.

Exact Text/cubic paths retain the same four actual malloc calls/order, original
work/edge N/N-1 outcomes, complete RGBA references and 40s Source/OOM timeout.
No original PDF/reference, CMake/workflow, installer or consumer project changes.

## Verification and remaining scope

The existing C11 device test destroys temporary page/PATH/Clip/GS/image
descriptors, limits, metadata and device values before executing retained PATH,
integer clipped RAW and physical 96-DPI RAW pages. It checks independent output,
retained byte credit, cumulative refusal, zero failure publication and cleanup.
The real Source/OOM executable owns actual heap IR for PATH/RAW/Clip fixtures,
checks preparation rollback, caller clearing, raw data lifetime, repeated RAW
placement billing and independent outputs after owner destruction. An existing
positive Text and original-TeXample render at each DPI now exercises actual IR
adoption and frozen limits/device values before full RGBA/work comparison; test
fixture ownership is moved back only after compiled disposal. No additional
expensive original render is added. Earlier command-destruction checks remain; all four real malloc failures now
exercise the adoption lifecycle, followed by the existing recovery renders.

The subsequent [Context/Composer integration](native-context-preparation-m1c.md)
binds the production Source owner to an existing Core Context reference and
provides an explicitly synchronous Composer adapter into the common compiler.
M1 acceptance is recorded only after exact-HEAD and final production CI; the
bounded foundation does not introduce a public retained-page ABI or cross-call
cache. M2 opaque Text/Image/Fill/Clip composition remains separate. Existing
Source profiles, original TeXample 72/96/144-DPI and yWorks 72-DPI evidence and
corpus counts remain unchanged. Exact HEAD/tree and real platform qualification
are recorded in the PR and #570.
