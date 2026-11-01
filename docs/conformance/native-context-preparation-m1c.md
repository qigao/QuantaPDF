# Context-bound preparation and synchronous Composer input — M1c

This increment of [#570](https://github.com/qigao/QuantaPDF/issues/570) supplies
the remaining M1 lifetime/adapter implementation for the existing admitted
profiles. Exact-HEAD and final production acceptance are recorded in the PR and
issue after real Native CI; implementation alone is not qualification. M2–M8
capability expansion remains separate.

## One Context lifetime implementation

The public render facade and the older qualified private facade acquire a
second temporary Context reference for native preparation through the existing
Core retain operation. The Bitmap keeps its established independent reference.
A private unique lease moves the preparation reference; its trusted release is
the existing Core drop function. It is not a provider/plugin interface, a new
reference counter, allocator or concurrency model. Acquisition requires an empty
lease; all mutation/retain/drop still requires external serialization.

The preparation lease protects Source admission before there is an IR owner.
Complete successful geometry preparation atomically adopts both the owned IR
and this reference. Failed admission/preparation leaves the reference with the
facade for cleanup and preserves the caller's IR ownership. Source-page disposal
releases pending geometry leases, retained geometry and IR, clears the owner,
then releases Context. A failed raster has the same cleanup order and no output.
Unconsumed facade leases are always released. Final RGB/Bitmap publication keeps
the existing independent ownership and Context lifetime.

The public Source path uses this binding for every admitted page. Old private
provider-free raster/IR entry points retain their signatures and can still run
without a Context. The new private Context entry points select exactly the same
Source profiles/compiler/painter; there is no retry or alternate pixel producer.

## Synchronous Composer adapter

A private adapter borrows the actual Composer IR under a caller-held retained
guard from the same Context. Composer mutation/drop and guard release are
excluded during the synchronous call. It uses the same resource closure, device
planner, compiled-page executor and ledger; no compiled handle or image-resource
pointer escapes. The returned plain RGBA owns independent bytes.

Only identity PATH transforms are normalized to the existing compiler's zero
identity sentinel in stack-local operation records. Nonidentity transforms are
refused. All committed resources and operations remain in the closure: no unused
font, Clip, GS, image, navigation or unknown operation can be silently removed.
Allocated spare slots are normalized as storage capacity, while their actual
checked byte sizes are added to both retained-byte inventory and the derived
live-byte envelope. Capacity underflow/overflow is refused before allocation.
Pixel, source/drawn image, edge, sample and operation/glyph quotas do not increase.

The adapter currently admits only what the existing compiler already accepts;
it does not add public native Composer rendering, alter Composer finish/export,
perform text layout/outline conversion or enable general Text/Image/Clip
composition. Public Composer mutation remains the owner of borrowed payloads.
Separate synchronous calls are separate render lifetimes; children within a
compiled page never reset budgets.

## Verification

The existing Source/OOM executable creates real SDK Contexts for its existing
owned PATH/RAW/Clip fixtures. It checks atomic lease rollback/adoption, last
reference retention, geometry/IR cleanup before Context release, independent
interleaved domains and pixels surviving both domains. Existing Text/original
TeXample positive and all four exact malloc-failure paths now use real Context
references; no extra expensive original-PDF render is added.

Actual public Composer calls create a clipped PATH + RAW page. Full independent
analytic RGBA references at 72/96/144 DPI verify the same compiler, source pixel
inventory, per-placement charges, and exact retained array-capacity bytes.
Sample N/N-1, capacity overflow, nonidentity transform, wrong Context, complete
IR immutability, recovery, unchanged finish/export and detached output lifetime
are checked inside that same existing executable.

The existing public UserUnit/OOM test separately forces preparation-reference
overflow after Bitmap retention, checks empty output and balanced references,
and retains its real Bitmap allocation failure/recovery/finalization checks.
Existing original Source positives/refusals, fixed RGBA, exact four raster malloc
calls/order, SDK export audits, 40s Source/OOM timeout and all numeric caps stay
unchanged. No CMake/workflow or installer/consumer project changes are needed.

Local strict C11/C++20 and ASan/UBSan cover the real Core retain/drop with local
Context storage fixtures, owned Source-page cleanup and the actual C11 Composer
adapter. Full public Context/Composer construction, QPDF and DLL/SDK behavior
require real CI. Local leak detection has the host's existing detect_leaks=0
restriction; the real CI sanitizer settings are unchanged.

## Scope after M1

The M1 foundation consists of common bounded preparation/resource closure,
owned geometry/IR, a stable page-wide ledger, retained Source Context lifetime
and an explicitly synchronous Composer borrow. This refactor adds no public
prepared-page ABI or cross-call cache. Those are separate interface/optimization
choices, not proof of additional PDF syntax. M2 opaque Text/Image/Fill/Clip
composition, broader device behavior, Stroke, Form, transparency and font
extensions retain their own admission and original-source acceptance gates.
