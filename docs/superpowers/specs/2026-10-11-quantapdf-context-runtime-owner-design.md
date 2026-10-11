# Context runtime ownership

Implemented in 2.34 for [#144](https://github.com/qigao/QuantaPDF/issues/144).
The installed API and runtime implement the retained-domain contract below.
It builds on the private backend boundary in
[#143 / PR #178](https://github.com/qigao/QuantaPDF/pull/178). The implementation
baseline for the observations below is commit `d294388`.

## Findings and compatibility constraints

These are risks when introducing a context, not claims that the current
implementation violates its contract.

| Severity | Evidence | Trigger and effect | Design constraint |
| --- | --- | --- | --- |
| HIGH | Fact: [structured-text lifetime test](../../../tests/test_structured_text.c), [form lifetime test](../../../tests/test_pdf_form.c), and [edit lifetime test](../../../tests/test_pdf_annotation_mutation.c) access objects after closing their source document. | A document-owned context or bulk context reset could invalidate surviving snapshots, edit graphs, or outputs. | Every owned handle retains its runtime domain independently of the source handle. Context release never recursively drops children. |
| HIGH | Fact: [text.c](../../../src/text.c) implements `quantapdf_free()` with `free()`. [render.c](../../../src/render.c) and [pdf_export.c](../../../src/pdf_export.c) also release C-allocated result storage. | Routing these allocations through an arbitrary context allocator would introduce mismatched frees or require an ABI change. | Keep allocator provenance explicit and retain the existing C allocator contract. Custom allocators require separate complete design under #157. |
| HIGH | Fact: [pdfium_runtime.cpp](../../../src/backend/pdfium_runtime.cpp) owns a process-wide once flag and recursive mutex. | Initializing or destroying PDFium per context could conflict with another live context; per-context locks would not serialize the engine. | Preserve the existing private process runtime and global engine lock. A context release does not destroy PDFium. |
| HIGH | Fact: the [API contract](../../../README.md#api-contract) requires serialization across unrelated handles as well as related ones. | Advertising concurrent contexts would permit calls outside the current runtime contract. | Context grouping does not grant concurrency. Changes to scheduling and synchronization belong to #161. |
| MED | Fact: [internal.h](../../../src/internal.h) has separate document and Composer storage; snapshot structures and [pdf_edit_internal.h](../../../src/pdf_edit_internal.h) have independent cleanup paths. | Adding context ownership only to documents would miss Composer outputs, edit-derived snapshots, image-rendered bitmaps, and failure cleanup. | Migrate every handle-producing path as one internal ownership change before exposing explicit-context factories. |

The [API contract](../../../README.md#api-contract) also requires documents to
outlive pages and rendered bitmaps. The image-occurrence lifetime test in
[test_images.c](../../../tests/test_images.c) drops the image snapshot before
closing its document. Some implementations currently copy all payload bytes;
that observation does not extend the supported lifetime of these handles.

## Decision and scope

Use a private reference-counted runtime domain exposed through an
opaque `quantapdf_context`. It is the owner of immutable runtime policy and any
context-local runtime resources. Each document, edit, Composer, snapshot,
bitmap, and output still owns and releases its own payload through its existing
drop function. The context does not own a list of these objects.

The caller owns one context reference after explicit creation. Every live
owned handle owns one internal reference. Releasing the caller's context
reference leaves existing handles usable under their original lifetime rules;
the domain is destroyed synchronously when its final reference is released.
This avoids a context-to-child-to-context reference cycle and keeps cleanup
local to the object that owns the payload.

The first implementation uses the built-in PDFium/qpdf provider and the current
C allocator. Document graphs, caches, resource registries, transactions,
schedulers, plugin registries, and arenas retain their own module ownership.
Those modules may later own bounded resources inside this lifetime boundary
through explicit dependencies; their algorithms remain with their modules.

## Ownership map

```text
caller context reference ----------------------+
document / edit / Composer / snapshot / output -+--> retained runtime domain
page / bitmap / image snapshot ----------------+
                                                       |
                                                       +-- immutable policy
                                                       +-- built-in provider selection
                                                       +-- context-local runtime resources

page -------- borrows document -------- owns provider document + source bytes
edit -------- owns a separate provider graph
snapshot ---- owns copied observation storage
```

The retain arrow protects the domain. Document lifetimes continue to follow
their existing borrowed-handle rules: applications drop document-dependent
handles before `quantapdf_close()`.

| Object or value | Domain reference and propagation | Payload/lifetime authority |
| --- | --- | --- |
| Explicit context handle | One caller-owned reference; the public pointer is consumed by its drop. | Domain state only. |
| Document | Retains the factory's domain. | Document owns copied input and password plus provider state; provider closes before input is freed and the password is wiped. |
| Page, rendered bitmap, image occurrence snapshot | Each owned handle retains the source domain. `quantapdf_image_render()` propagates the image snapshot's domain. | Existing document dependence is preserved, including image-specific lifetime rules; a context reference cannot substitute for the required live document. |
| Text, links, outline, annotation, and form snapshots | Retain the producing document's or edit's domain independently. | Copied observations remain usable after source close; accessor pointers expire at snapshot drop. |
| Edit | Retains the source domain, never the source document. | Edit owns its independent graph and immutable edit ops; source close does not affect mutation or snapshot creation. |
| Composer | Retains its factory's domain. Temporary form/pattern child Composers inherit it. | Composer owns copied inputs, operations, and resource tables. Existing local limits remain authoritative. |
| Output | Retains the producing document's, edit's, Composer's, or merge destination's domain. | Output owns serialized bytes; source drop does not invalidate them. |
| Caller-owned `char **` results | No retained domain; storage is detached before publication. | Caller uses `quantapdf_free()` even after all handles and the domain are gone. |
| Borrowed accessor pointers, scalar info, resource IDs, edit references | No additional domain reference. | Their existing owner and invalidation rules apply. Sharing a context does not make IDs portable between Composers or edits. |

An output merge accepts borrowed serialized bytes from multiple contexts and
creates a new output in its destination context. Input ownership does not move.
This is already a byte-based operation; it does not authorize sharing native
document objects, edit references, or Composer resource IDs across owners.

## Domain state and destruction

The private domain contains a checked reference count, immutable default
allocation policy, and explicit built-in provider selection. The first version
needs no child registry, global default context, thread-local current context,
background worker, or finalizer queue.

The reference invariant is:

```text
references = caller-owned references
           + references owned by published handles
           + references held by construction/operation guards
```

These terms describe ownership roles, not three independently maintained
counters. The implementation has one authoritative reference count. An explicit
create publishes one caller reference. An implicit factory starts with a
construction guard and transfers it to the new root handle. Each successful
derived-handle factory transfers one acquired reference into its result.
Borrowed pointers and numeric IDs never participate in the count.

```text
ALLOCATING -- initialize successfully --> LIVE (references > 0)
     |                                     |
     +-- failure: free unpublished state   +-- last release --> FINALIZING --> freed
```

Dropping the caller reference while handles remain is a `LIVE -> LIVE`
transition. It does not mark the domain closing, stop existing operations, or
cancel work. A surviving document may still load pages and produce snapshots;
a surviving edit may still mutate and produce outputs. The dropped public
context pointer is no longer a caller-owned capability and must not be reused,
even if another handle still keeps the underlying domain alive.

Retain checks overflow before incrementing; an unrepresentable count fails
with `QUANTAPDF_ERROR_UNSUPPORTED` before provider work or publication. Storage
allocation failure remains `QUANTAPDF_ERROR_NOMEM`. A release from zero is an
internal invariant violation, never a saturating decrement. Reusing a freed
handle or dropping one owned reference twice remains invalid caller behavior;
the design does not add a stale-pointer lookup registry.

Every handle destructor follows this order:

1. Save its domain pointer in a local variable.
2. Release provider objects and owned payloads while the domain remains live.
3. Free the handle with its allocation domain still available.
4. Release the saved domain reference exactly once.

On the final reference, destroy context-local resources in reverse dependency
order, then free the domain. Context-local resources must not retain the
context that owns them. They must not invoke public callbacks, resurrect
handles, wait for outstanding work, or depend on another already-destroyed
resource. Future asynchronous work must own its own domain reference until
completion; its cancellation and drain protocol is separate work under #161.

## Construction, publication, and rollback

Factories preserve existing argument-validation and output-reset behavior.
They acquire a domain guard before any callback or backend work that could
outlive an outer caller reference. The result is unpublished until allocation,
provider initialization, and ownership attachment all succeed.

On success, move the guard reference into the handle and publish its output
pointer. On failure, destroy all partial provider/payload state, release the
guard, leave the output empty, and propagate the original status. Attaching
the domain is an internal pointer transfer with no late allocation. It must
not create a new failure after a mutating operation has committed.

For snapshots currently allocated inside an engine bridge, the facade holds
the guard across extraction and attaches the domain before public publication.
Unpublished bridge cleanup and published snapshot cleanup must have explicit,
distinct ownership of that reference. Common private attach/drop helpers may
enforce the invariant, but engine allocation does not silently retain a
second reference. Apply the same rule to form-model wrapping, bitmap creation,
edit snapshot wrapping, Composer finish, and output transforms.

Composer form/pattern builders already run synchronously with a temporary
child Composer. Preserve that child ownership: the builder borrows the child
for the call, and must not drop or keep it after returning. The parent and
child references keep the domain alive across the callback and rollback. A
callback may release a separate context reference that it actually owns;
doing so cannot free the domain beneath the active Composer. It does not gain
permission to destroy the parent/child or reenter an in-progress mutation.

The context introduces no mutable process-wide defaults. Failed construction
in one context cannot overwrite another context's provider selection, limits,
allocator policy, or cleanup state.

## Allocators, capacity, and provider runtime

The first domain has fixed-size storage and retains no growing list of child
objects. Reference arithmetic is checked against `SIZE_MAX`. Existing source,
render, transform, and Composer allocation/size limits remain with their
current owners. This implementation adds no aggregate resident-memory limit and
does not claim that the context measures every byte allocated by qpdf, PDFium,
the C++ runtime, or the caller.

All current result storage keeps its allocator provenance. In particular,
detached `char **` results are not context allocations that happen to survive
context destruction; they are C-allocator-owned results from the start.
`quantapdf_free()` continues to take only the returned pointer. Do not prepend
an undocumented context header or reinterpret arbitrary malloc-compatible
results to discover a hidden allocator.

A future allocator/arena design under
[#157](https://github.com/qigao/QuantaPDF/issues/157) must specify allocator
callbacks, alignment, userdata lifetime, allocation origin, byte accounting,
overflow-safe quotas, OOM rollback, and provider allocations outside the quota.
Any storage using context-local deallocation must retain that domain until
after its final free. Raw C results must retain their existing matching-free
contract or use a separately reviewed additive owned-buffer API. Arenas cannot
reset beneath published snapshots. No allocator callback API or pool is part
of the first context implementation.

For the built-in provider, [document_backend.h](../../../src/backend/document_backend.h)
already supplies process-lifetime immutable ops. The context selects those ops
explicitly; documents and edits retain their existing provider state. PDFium
initialization remains lazy at its existing entry point, and its private
process-wide lock continues to cover all contexts. Neither dropping a context
nor reaching zero context references calls `FPDF_DestroyLibrary()` or resets
the once flag. qpdf document/edit objects keep their existing local cleanup.

Context-owned provider runtimes, unloadable provider code, and capability
negotiation require their own admission and lease contracts under
[#158](https://github.com/qigao/QuantaPDF/issues/158) and
[#172](https://github.com/qigao/QuantaPDF/issues/172). A domain reference alone
must never be treated as a plugin code lease. The existing process PDFium
runtime is an explicit backend constraint, not an application service locator.

## Threading and shutdown semantics

All `quantapdf_*` calls remain externally serialized across the process under
ABI v2, including calls on distinct contexts and all drops. The initial
reference count therefore need not be atomic. An atomic count alone would
not make providers, handles, payload mutation, or borrowed pointers safe for
concurrent use.

There is no cancellation, blocking close, reset, forced recursive destruction,
or independent context stop operation in this implementation. Dropping a context is
a reference release; the caller uses the existing object drops to end object
lifetimes. Cleanup executes on the thread making the last release, within the
existing serialization contract. The synchronous runtime has no queue to
drain and no work that is silently abandoned at this boundary.

This lets a later [parallel runtime design](https://github.com/qigao/QuantaPDF/issues/161)
add operation references and an explicit stop/drain owner without changing
what an existing context drop means. Thread-affine provider teardown, if ever
required, needs a separately specified scheduling and shutdown contract.

## Additive API and legacy entry points

The installed header exposes the following additive API surface:

```c
typedef struct quantapdf_context quantapdf_context;

quantapdf_status quantapdf_context_create(quantapdf_context **out_context);
void quantapdf_drop_context(quantapdf_context *context);

quantapdf_status quantapdf_open_with_context(
    quantapdf_context *context, const char *filename, const char *password,
    quantapdf_document **out_document);
quantapdf_status quantapdf_composer_create_with_context(
    quantapdf_context *context, const quantapdf_composer_options *options,
    quantapdf_composer **out_composer);
quantapdf_status quantapdf_merge_outputs_with_context(
    quantapdf_context *context, const quantapdf_output *const *inputs,
    size_t input_count, quantapdf_output **out_output);
```

| Entry point | Contract |
| --- | --- |
| `quantapdf_context_create(out_context)` | A NULL `out_context` returns `ARGUMENT`; a valid slot is reset to NULL before allocation. Success returns `OK` and one caller-owned opaque context reference with immutable built-in/default allocation policy; allocation failure returns `NOMEM`. No user callback or PDFium initialization runs here. |
| `quantapdf_drop_context(context)` | NULL-safe; consumes the caller's one owned reference. Existing owned handles continue under their own references and original source-lifetime rules. There is no public retain/clone operation in this first surface. |
| `quantapdf_open_with_context(context, filename, password, out_document)` | Requires a live caller-owned context; remaining input/error behavior matches `quantapdf_open`. The context is borrowed during the call and the successful document retains it. |
| `quantapdf_composer_create_with_context(context, options, out_composer)` | Requires a live caller-owned context; retains it on successful creation. Existing Composer options, defaults, and limits keep their meanings. |
| `quantapdf_merge_outputs_with_context(context, inputs, input_count, out_output)` | Requires a live caller-owned destination context; borrows input outputs synchronously and retains only the destination domain in the new output. Existing merge validation and status propagation apply. |

Explicit-context factories reject NULL contexts with `QUANTAPDF_ERROR_ARGUMENT`
and reset valid output slots before returning an error. They do not select an
implicit default when a context argument is invalid. Before construction they
can also return `UNSUPPORTED` for reference-count overflow, as described above.
There is no context-options record containing unused placeholder settings;
future non-default policy needs a separately reviewed additive creation API.

Existing `quantapdf_open`, `quantapdf_composer_create`, and
`quantapdf_merge_outputs` remain exported with the same signatures. Each
creates an unpublished private domain, transfers its initial reference into
the successful root result, and otherwise destroys it during rollback. This
avoids an immortal global default domain. Derived objects inherit the source
domain through private constructors, including temporary Composer children.
Legacy factories preserve their argument and NULL-output contracts; the
additional domain allocation can fail with the existing
`NOMEM` status.

Usage:

```text
create context C                     -> caller owns C
open document D with C               -> D retains C
begin edit E from D                  -> E retains C; owns independent graph
extract a text snapshot S from D's page -> S retains C; owns copied observations
drop the page, then close D          -> E and S remain usable
drop caller reference C              -> E and S still keep the domain live
snapshot E into output O            -> O inherits a domain reference
drop E and S                         -> O remains usable
drop O                               -> final domain cleanup, if no other handles
```

The public C11 header and export baseline add these five entry points in 2.34.
Opaque object layouts grow privately. Existing
symbols, enum values, option/info layouts, and caller ownership obligations
remain compatible with ABI v2. The context object is never a raw PDFium/qpdf
handle, and adding these entry points does not require those types in the
installed header.

## Alternatives and tradeoffs

| Alternative | Consequence | Decision |
| --- | --- | --- |
| Children borrow the context; caller must drop context last | Adds a new lifetime precondition to otherwise independent snapshots and makes implicit legacy contexts difficult to reclaim. | Use child-owned domain references. |
| Context owns a child registry and recursively destroys it | Invalidates externally held opaque handles and accessor pointers; adds a growing registry and teardown ordering machinery. | Keep payload cleanup with each handle. |
| Checked destroy refuses while children remain | Can support an explicit strict owner, but introduces busy/retry control flow and needs different implicit-context cleanup rules. | Prefer one release/final-reference rule for both explicit and legacy paths. |
| Process-global or thread-local default context | Hides provider/policy selection and couples unrelated callers' lifetime or configuration. | Create a private domain for each legacy root operation. |
| Move all storage to a context arena immediately | Changes result deallocation and reset semantics, adds retention costs, and exceeds the allocator work needed by #144. | Preserve allocator provenance; develop bounded arenas separately. |

The cost is one fixed-size domain allocation per legacy root creation,
additional private handle pointers, and checked reference operations. Retained
outputs may keep context-local resources alive after their producer closes.
Avoid putting heavyweight caches in the first context. No performance or
memory reduction is claimed without measurement.

## Implementation sequence and rollback

1. Land the private backend boundary from PR #178. Implement private domain
   create/retain/release helpers and complete propagation through document,
   page, bitmap, image-render, every snapshot type, edit, Composer and merge
   factories, plus all success and failure cleanup paths. Keep the domain
   private until the full ownership matrix is covered.
2. Verify legacy APIs against their existing tests and add focused lifecycle
   and allocation-failure cases to formal test targets. This stage adds no
   public symbols. Reverting its private fields/helpers and propagation
   restores the former implementation with no data migration.
3. Add the explicit-context factories as one complete ABI extension,
   with installed-header contracts, version/export updates, usage examples,
   and the same failure/lifetime tests exercised through explicit factories.
   Test both static and shared builds. Keep legacy entry points as wrappers.
4. Develop aggregate quotas/arenas (#157), resource registry (#147), immutable
   snapshot unification (#148), transactions (#153), and scheduling (#161)
   against the retained-domain contract. These are separate changes rather
   than requirements to implement empty modules in the first context.

Once new symbols are released, rollback must preserve them and their documented
semantics; removing them would break ABI v2. Internal rollback can retain the
domain facade and original provider/storage implementation. Any future change
to public source-lifetime or drop semantics requires separate ABI review under
[#171](https://github.com/qigao/QuantaPDF/issues/171).

## Verification requirements

The implementation is exercised by `quantapdf.context` and the existing
backend, extraction, transform, edit and Composer CTest targets. The private
per-context test controls inject facade allocation failure and observe final
domain release without adding a process-global test allocator or public ABI.
They do not inject every allocation inside the third-party engines.
The verification contract is:

- Explicit create/drop, NULL drop, factory argument errors, output reset,
  retained-reference overflow, and allocation failure at every construction
  boundary leave no provider/payload/domain leaks or half-published handles.
- Drop the caller context before documents, edits, snapshots, and outputs;
  exercise their permitted operations and every drop, and observe exactly one
  final domain cleanup. Repeat with multiple documents sharing a context.
- Close a source document before edit mutation/snapshot and independent text,
  link, outline, annotation, form, and output access. Keep the established
  page/bitmap/image document-lifetime preconditions in all valid-use tests.
- Exercise image-derived bitmap propagation, all transform outputs,
  Composer finish repeatedly, nested form/pattern child creation, and builder
  failure. Releasing a separately owned context reference inside a permitted
  builder callback cannot invalidate its active domain.
- Verify detached raw strings still survive domain destruction and are freed
  with `quantapdf_free()`; borrowed accessor pointers retain their existing
  owner and invalidation point.
- Verify independent contexts cannot mutate one another's runtime policy;
  cross-context serialized-output merge succeeds without moving input owners,
  while owner-local IDs retain their existing restrictions.
- Repeated context create/drop must not reinitialize or destroy the shared
  PDFium process runtime. Provider drop occurs before final domain release.
- Run the existing formal CTest suites through Release and sanitizer user
  presets, including backend foundation, document/render/extraction,
  transform/security, form/annotation edit, Composer, and shared ABI export
  checks. New concurrency testing belongs to a future concurrency change;
  do not claim it from this serialized design.
