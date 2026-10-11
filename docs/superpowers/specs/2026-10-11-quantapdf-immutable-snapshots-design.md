# Immutable snapshot ownership

Implementation decision for [#148](https://github.com/qigao/QuantaPDF/issues/148),
on the resource-registry baseline `f2d18b8`.

## Evidence and scope

MED, fact: text, links, images, outlines, annotations and forms each store a
retained context in their private handle and repeat the same finalization order
in `src/snapshot/`. Document and edit form constructors share the same payload
model. Backend extraction builds owned copies before the facade publishes them;
link coordinate normalization is part of that unpublished construction phase.
Public getters already take const snapshot handles and return copied scalars or
views into their owner's storage.

Metadata and plain text extraction return detached `char **` allocations, not
snapshot handles. The native SDK contract requires `quantapdf_free` for them.
Image snapshots still require the source document to outlive them. Retaining a
context does not relax that public lifetime rule.

## Decision and protocol

Embed one private `quantapdf_snapshot` header as the first member of each typed
snapshot handle. It holds the retained context and the payload finalizer.
Centralize final release in the snapshot module: save the context, destroy the
typed payload and its allocation, then release the context. Typed public drop
functions delegate to this path. No public generic handle, refcount, type tag,
reflection system, allocator, or dependency is added.

Construction reserves the context reference before backend work, preserving the
existing reference-overflow failure behavior. Binding the header consumes that
reference without another allocation or fallible step. A bound object may still
be privately normalized or have its form model populated; it becomes immutable
only when the successful facade call publishes it through the output pointer.
Failure before binding releases the reserved context; failure after binding uses
the same finalizer as normal release. The backend retains responsibility for
cleaning its own partially constructed, unbound objects.

The lifecycle is unpublished construction -> published immutable observation ->
drop. The handle uniquely owns its payload. Every borrow ends when that handle
is dropped, regardless of another snapshot's content or the source's lifetime.
There is no mutation, reset, refresh, moving, shared ownership or asynchronous
consumer in this change. Calls and destruction remain externally serialized.
Existing extraction limits and allocation/status failures are unchanged. No new
queue, staging copy, logging path, or memory budget is introduced.

Text/link/outline/annotation/form snapshots remain independent of subsequent
edits and may outlive their sources. Image snapshots retain their existing
document-lifetime restriction. Detached metadata/text buffers follow the same
observation/copy semantics but retain their established free-based ABI; wrapping
them in a hidden owner would break that ABI. These exceptions are explicit in
the SDK documentation.

## Alternatives and future boundaries

Keeping six independent finalization paths leaves the runtime-release order as a
per-feature convention. A generic tagged union would couple all payload layouts
and replace existing typed accessors. A shared arena or public reference-counted
snapshot would change allocation and lifetime behavior without a present need.
The common owner and typed payloads share only the actual lifetime protocol.

A future Document Graph may supply copied payloads through the same publication
boundary. CMeta descriptors may describe those immutable payloads, but cannot
extend a borrow or confer mutation authority. Neither feature is implemented by
this ownership refactor. No source-document pointer or generation claim is
invented before the graph has its own contract.

## Verification and migration

Update the six private handle layouts, construction/drop paths, image-to-bitmap
context lookup, edit form construction, and internal lifecycle tests together.
Public declarations and exports stay unchanged. Reverting this commit restores
the earlier private layouts; no persisted-data migration is needed.

Verify runtime finalization ordering, one retained context per snapshot, partial
construction failure, detached-source lifetime and borrowed-view stability with
formal tests. Run all snapshot/context/edit tests and ABI checks, followed by
Release and sanitizer CTest suites, SDK installs and Linux static CI. Existing
provider-output and mutation regression tests remain the behavioral authority.
