# Native Source preparation and resource closure — M1a

M1a of [#570](https://github.com/qigao/QuantaPDF/issues/570) separates the
existing Source bridge's route selection from C11 execution. The same QPDF
interpreter first completes and publishes its detached owned IR. The bridge
then prepares one private synchronous borrowed plan, executes exactly one
existing consumer and disposes the sole Source IR owner on every exit.

## Preparation

The allocation-free C11/C++20-compatible helper validates bounded operation
storage, referenced Image/GS/Clip IDs, topologically ordered Clip DAG closure,
all registered resources' reachability, exact capacities and hidden zero-count
resource pointers. It rejects unknown operation/resource kinds. Checked
inventory counts original operations, expanded glyph paints, commands, glyphs,
unique retained image pixels and bytes. Retained payload is counted once per
owned record; repeated painting does not imply free execution work.

Preparation snapshots the caller's raster caps and the selected existing
route. Text and exact-cubic paths prepare their existing physical device plan;
other routes retain the existing device projector. The original geometry and
page dimensions are unchanged. Only Source's existing V8/V9/V10 contracts are
selected; there is no failed-render retry or new Source syntax.

This is a **resource/dispatch preparation boundary**, not a claim that all
kernel geometry has been compiled. Existing consumers retain their exact
geometry/state-order admission and actual raster work/edge accounting.
Preparation success alone is not public rendering acceptance.

## Execution and work evidence

The immutable plan passes the same frozen page caps to exactly one existing
C11 consumer. Existing conservative child partitions remain unchanged.
Text/exact-cubic consumers return an actual sampled-work receipt; other
consumers explicitly mark that measurement unavailable. A zero counter with
the exact flag unset must never be reported as zero actual work. Failure
clears both pixels and the optional execution receipt.

M1a does **not** finish M1's common mutable work ledger or per-primitive
compiled geometry. Those remain follow-up work before M1 can be checked off.
The current 4-paint, 64-glyph, 4096-command, 512-edge, 1M-pixel and shared 8M
public limits are not increased. No new rendering feature is admitted.

## Existing acceptance targets

- The pure-C11 device test checks all 72/96/144-DPI pixels against its existing
  consumer, immutable plan/input data and caller-limit snapshot isolation.
  Missing/hidden resources, unused GS/Clip, invalid membership, cycles,
  capacities, indices and unknown operations must reset the prepared output.
- The real Source/OOM test prepares actual owned TrueType/rectangle IR with
  zero observed Source C/C++ allocations. Its existing whole-image, N/N-1,
  edge boundary and all four actual raster malloc-failure/recovery checks
  execute through preparation. Explicit malformed private physical-plan
  controls retain their original direct-kernel checks.
- Original TeXample shared physical 72/96/144-DPI execution, exact N/N-1,
  edge boundary and actual malloc-failure/recovery use the prepared route.
  The legacy private route and explicit malformed-kernel controls remain
  separately tested. All reference images and external originals are fixed.
- All public original PDF/Bitmap, Clip, resource, page and corpus tests run
  through the changed production bridge. No additional test target, CMake
  project, consumer/installer project or workflow is added.

Ownership remains synchronous: the plan borrows the complete immutable IR,
owns its scalar snapshot only, and must not outlive or mutate that owner.
Completed pixels remain independent after the bridge disposes Source IR.
Public handle/Context ownership and external-serialization rules are unchanged.

Exact HEAD, full tree and real CI results belong in the associated PR. Passing
an isolated preparation test does not substitute for original public PDF
rendering or qualify Text/Image/Clip composition planned for M2.
