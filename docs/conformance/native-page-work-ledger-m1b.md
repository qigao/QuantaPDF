# Native page sample/edge reservations — M1b.1

M1b.1 of [#570](https://github.com/qigao/QuantaPDF/issues/570) adds one mutable,
allocation-free work ledger to each execution of the immutable M1a Source
plan. It covers every existing production V8/V9/V10 route: legacy/device
PATH/RAW Image, Image/Clip masks, compound/prefix/sibling/three-GS Clip
profiles, Text outlines and the original three-layer TeXample classifier.
It uses the same device projector, geometry validators and C11 pixel kernel.

## Reservation and settlement

The page caps are copied from the prepared plan. A child reserves its complete
existing sample-work and edge allowances **before** raster allocations or
sampling. At every transition, committed work plus outstanding reservations
fits the page caps. All arithmetic is checked by subtraction, including at
`SIZE_MAX`. Leases are synchronous, uniquely owned borrows; no global or
thread-local ledger, allocation, callback, lock or provider is introduced.

On success, the child settles the existing deterministic charged sample work
and the actual number of retained flattened edges. Unused reserved credit
returns to the page. The old per-layer divisions and local caps remain fixed:
returning credit does not give a later layer a larger share or admit a formerly
refused page. A repeated Clip compiled for another layer is charged again;
the separately qualified original cubic shared geometry retains its existing
unique-edge accounting.

A failed sampler conservatively commits its entire reservation and poisons
the page ledger. A compositor failure also poisons the ledger and destroys all
private layers. That ledger cannot be used to retry another paint. A fresh
public execution gets a new ledger, allowing normal OOM recovery. Pixels and
the optional execution receipt are published only on complete success;
every failure clears them. Re-executing a prepared plan never mutates it.

## Receipt meanings

| Field | Meaning |
|---|---|
| `charged_sample_work` | Sum of the existing kernel's billed sample units; conservative upper bounds on older PATH/Clip routes, actual deterministic classification counts on Text/exact cubics |
| `sample_tests`, `sample_tests_exact` | Actual classifier count only when exact; otherwise zero with the exact flag unset, which must not be called zero actual work |
| `compiled_edges` | Actual flattened edges retained by successful child samplers, summed across the page |
| `peak_reserved_sample_work`, `peak_reserved_edges` | Largest outstanding reservation in each dimension, separately from already committed work |
| `completed_leases` | Number of successfully settled geometry consumers; a raw integer Image without a geometry mask can use zero leases |

These units are unchanged. An upper-bound edge bill is not an instruction,
time, allocation or exact geometric-predicate count. An Image without a mask
spends no sample/edge units; its source/destination pixel work is still bounded
by the existing independent page preflight counters.

## Acceptance

The existing pure-C11 device test covers two actual raster calls sharing a
ledger, refusal of a third full-budget draw, two-layer ordered pixel equality,
aggregate sample/edge caps one below the required amount, late invalid paint,
held reservations, unused-credit settlement, double settlement, poisoned-page
refusal and overflow. Existing 72/96/144-DPI prepared-plan comparisons now also
inspect charged work, edge counts and reservation receipts.

The existing actual Source/OOM test checks exact receipts for Text and original
TeXample 72/96/144-DPI execution, N/N-1 and edge boundaries, every existing
malloc-failure/recovery path, unchanged four raster allocations, immutable
Source IR and complete fixed RGBA references. Public original PDF/Bitmap,
resource, Clip and corpus checks exercise the changed production bridge.
No test target, workflow, CMake, installer or consumer project is added.

## Remaining M1 work

[M1b.2a resource accounting](native-page-resources-m1b.md) subsequently moves
the Source/placement pixel inventory and requested C11 heap bytes into this
same page ledger. The remaining geometry preparation boundary stays open.

M1 stays **open**. Per-primitive geometry compilation still occurs inside the
existing kernels; prepared plans do not yet own compiled geometry. That is
M1b.2b work, before M2 opaque Text/Image/Fill/Clip composition. This increment changes no Source syntax,
public ABI, original PDF/reference image, corpus-positive count, physical page
mapping, error recovery policy or numerical quota. TeXample remains qualified
at physical 72/96/144 DPI; yWorks remains qualified at 72 DPI only.

Exact HEAD, complete tree and real CI results are recorded in the associated
PR and #570. Local ledger checks do not substitute for public PDF acceptance.
