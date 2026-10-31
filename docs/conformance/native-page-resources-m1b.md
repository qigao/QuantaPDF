# Native page pixels and requested heap bytes — M1b.2a

This increment of [#570](https://github.com/qigao/QuantaPDF/issues/570) extends
the existing execution-owned sample/edge ledger to unique Source Image pixels,
destination placement work, borrowed Source IR payload and C11 raster heap
buffers. It applies to all currently admitted production V8/V9/V10 routes.
No new PDF syntax, resource composition, device option or public ABI is added.

## Pixel ownership

The existing V5 preflight validates every registered RAW resource and every
placement, including the original hard caps and Clip/state ordering. After
complete admission it atomically commits its pixel inventory to the page
ledger, before any child raster allocation. Source pixels are counted once
per registered owned Image; every repeated placement bills its enclosing
device-pixel rectangle. Clip visibility does not waive this bill.

Image/Clip helpers borrow the parent's pixel bill. They retain their original
local validation/caps and charge their own heap buffers to the same ledger.
Nested RAW/mask painting cannot double-charge Source pixels, reset placement
work or obtain independent page credit. Sample/edge leases and their fixed
child partitions remain unchanged. Pixel quota exhaustion publishes no output
and cannot partially commit only one dimension of the inventory.

## Requested-byte ownership

Preparation remains allocation-free. It snapshots resource caps and computes
a checked heap envelope from existing bounds: borrowed Source IR bytes, six
RGBA surface capacities (24 bytes per allowed output pixel), and the real
kernel type sizes for edge/draw/optional edge-box capacities. Four original
layers can retain three earlier RGBA images while a clipped Image owns raw,
mask and composite buffers. Text/exact-cubic masks fit inside this envelope.
This makes the existing bounded allocation topology explicit; it does not
increase numerical pixel, operation, glyph, edge or sample-work quotas.

The ledger registers already-owned Source payload as a borrowed byte floor.
Every subsequent C11 malloc reserves its requested bytes first. A byte quota
refusal is UNSUPPORTED and does not invoke the allocator; an actual malloc
failure is NOMEM and returns that reservation. Existing exact-raster OOM hooks
remain in the same allocation order, after reservation. There is no allocation
header, hidden heap metadata, extra malloc, new allocator or fallback engine.

Every free returns the exact requested byte count, including on failed pages.
An RGBA buffer transferred between child and compositor retains its charge
until the owning parent frees it. On successful native handoff, live bytes must
equal borrowed Source bytes plus the detached output buffer; all scratch and
intermediate layers must have been released. On failure, live bytes must return
to the Source floor and the complete output/receipt is cleared. The final
buffer then retains the existing ordinary image-dispose contract.

These are **requested payload/heap bytes**, excluding allocator overhead,
stack storage and QPDF parser internals. QPDF Source parsing and the later
public Bitmap/Context adapter retain their separate existing limits/ownership;
this is not a whole-process RSS or public allocator-domain measurement.

## Receipt and acceptance

The existing work receipt additionally reports unique `source_pixels`,
cumulative `drawn_pixels`, borrowed `retained_bytes`, detached `output_bytes`
and `peak_live_bytes`. Failure clears all fields. Source inputs and the
prepared plan remain immutable; a fresh execution owns independent counters.

The existing C11 device executable verifies repeated unclipped Images and
single clipped Images at 72/96/144 DPI, exact Source/placement caps, independently
counted simultaneous RAW buffers, measured byte peak N/N-1, complete reset and
recovery, atomic inventory refusal and SIZE_MAX/borrowed-floor arithmetic.
It also preserves the older clipped IMAGE-only exact-capacity refusal for
two placements sharing one resource; accounting does not widen that profile.

Existing actual Source/OOM tests inspect V8/V9 Image and V10 Text/cubic resource
receipts. Their existing positive sample/edge/OOM recovery executions reuse the
first observed exact heap peak. No additional expensive original PDF render is
added; sample N/N-1, all four malloc failures, full fixed RGBA images and the
original 40-second Source/OOM gate remain intact. No workflow, CMake target,
installer or consumer project is added. Exact HEAD/tree and actual CI results
are recorded in the associated PR and #570.

## Remaining M1 work

**M1 stays open.** Per-primitive geometry is still compiled inside the existing
kernels; the outer prepared plan does not yet own compiled geometry. That is
M1b.2b. M2 opaque Text/Image/Fill/Clip composition remains subsequent work.
Original TeXample physical 72/96/144-DPI and yWorks 72-DPI evidence, original
PDFs/reference images and corpus-positive counts remain unchanged.
