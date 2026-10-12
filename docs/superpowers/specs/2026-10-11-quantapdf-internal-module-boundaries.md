# Internal module boundaries

Implementation decision for [#150](https://github.com/qigao/QuantaPDF/issues/150),
following the backend, retained-context and native SDK changes (#143, #144,
#141). The baseline is `c17d006`.

## Evidence and decision

MED, fact: the baseline `src/internal.h` combines document/provider handles,
bitmap/output storage, all Composer operations/resources, test fault state and
unrelated snapshot headers. Most C facades include it, and the qpdf Composer
adapter includes the same file. A change in one owner's layout therefore
reaches unrelated owners and obscures their dependency direction.

Split declarations and source registration by existing responsibility. Keep
the same public library and private backend archive, with module-owned CMake
source lists. Remove the aggregate private header and use explicit includes.
No public declaration, exported symbol, resource identifier, allocation policy
or object layout semantics changes in this split.

| Module | Existing responsibility moved here | Dependencies |
| --- | --- | --- |
| `core` | Retained context, status/free, input/output file boundaries, bitmap and serialized output storage/access/drop. | Public C types; the context selects the immutable backend interface. No document or Composer state. |
| `document` | Document/page lifecycle, rendering, transforms, metadata and isolated edit facades. | Core, backend operation contracts, required snapshot payload types. |
| `snapshot` | Immutable text/image/link/outline/annotation/form observations and accessors; text search. | Core and public value types. Extraction entry points additionally use the document owner; payload headers do not. |
| `composer` | Builder limits, append/commit operations, navigation, fonts, SVG/barcode lowering and finish. | Core, resource records, operation records and private backend bridges. No document/snapshot state. |
| `resource` | Actual font/image/form/paint/clip/graphics-state records used by the existing Composer and writer. | Public value types. Composer still owns storage and IDs. |
| `ir` | Actual page and draw-operation records already produced by Composer and consumed by the writer. | Public value types. These remain the current Composer vocabulary. |
| `backend` | Provider dispatch, PDFium/qpdf implementations, codecs and text/font adapters. | Public values and the specific immutable/internal payload or Composer declarations each adapter consumes. |

The resource and operation headers carry working data structures; they add no
placeholder factories or empty registry/IR runtime. The unified registry (#147),
shared QP IR semantics and lowering (#145/#151), and Document Graph (#146) remain
separate implementation work. This split supplies their ownership boundaries
without claiming those systems already exist.

## Ownership, failure and compatibility

Each owned handle still retains exactly one domain reference. Its destructor
releases provider/payload storage, frees the handle, then releases the domain.
Moving bitmap/output access and destruction into core removes their incidental
dependence on rendering/export entry points. The output allocation helper moves
from context code to the output owner; context code no longer needs an output
layout. Detached string release stays matched to the C allocator.

Documents retain their source/password/provider state. Pages and bitmaps keep
their existing document-lifetime preconditions. Snapshot facades acquire the
domain guard and attach it before publication; provider rollback is unchanged.
Composer keeps the single authority for limits, resource IDs, operation counts
and storage. Resource records do not gain independent lifetimes or a second
registry. No new allocations, callbacks, threads, queues or fallible commit
steps are introduced. Existing errors and cleanup paths remain authoritative.

The public header, ABI version and export baseline are unchanged. Private tests
include only the owner declarations they inspect. Snapshot payload definitions
remain C-compatible, and backend types remain absent from installed headers.

## Alternatives, migration and rollback

Keeping a forwarding `internal.h` would preserve the unwanted dependency graph,
so all production and test consumers move to explicit module includes. Creating
a separate static library for every directory would change linking/packaging
and introduce ordering concerns without an independently consumable contract;
retain the existing two targets and let subdirectories own their source lists.
Replacing the resource or operation model during a file split would combine
behavior changes with ownership migration, so that work remains in its issues.

All source moves remain inside `src/`; CMake and current linked design references
move with them. Source customers need no migration because private headers are
not installed. Reverting this commit restores the former private layout and
source lists; PDF data, public handles and package targets need no migration.

## Verification

Use the formal context/backend tests first, then the full existing Release and
sanitizer CTest suites. Their scope includes independent snapshot/edit/output
lifetimes, allocation/overflow failure, Composer callbacks and resource use,
transform behavior, and real DLL export tables. Normal preset installation and
Linux static CI verify that the module-owned lists preserve both library graphs.
No test is added merely to assert source filenames or CMake text. Report actual
results for this commit, including platforms not executed.
