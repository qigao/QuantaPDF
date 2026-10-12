# Document graph, identity and transaction boundaries

Architecture decision for [#146](https://github.com/qigao/QuantaPDF/issues/146),
[#156](https://github.com/qigao/QuantaPDF/issues/156) and
[#153](https://github.com/qigao/QuantaPDF/issues/153), following the validation
foundation in #186. The findings below describe that baseline. Runtime
references, bounded graph import and private immutable graph commits are now
implemented in the sections below. A private, policy-restricted graph writer now
supports detached snapshot output. Feature-authority migration, encrypted/signed
graph output policy and graph-to-IR conversion remain outstanding.

The [private query layer](2026-10-11-quantapdf-graph-query-design.md) enumerates
stored objects, pages and members with stable references and resolves inherited
page resources. Metadata is a consumer of the same member lookup. Cursors borrow
one immutable version and require no provider calls or result allocation.

## Evidence and problem

MED, fact: `src/document/document_internal.h` owns input bytes, a password and an
opaque backend. `src/backend/document_backend.h` isolates provider calls, but
pages, resources and object relationships are still discovered inside providers.
There is no provider-independent object lookup or common graph snapshot.

MED, fact: `src/ir/ir.h` owns Composer pages, drawing operations, seven resource
families and navigation. `qpdf_composer.cpp` consumes that model to write PDFs.
The model does not represent arbitrary imported PDF dictionaries, object cycles,
catalog state or uninterpreted streams. A document graph and drawing IR therefore
need distinct representations with explicit conversion boundaries.

MED, fact: `quantapdf_qpdf_edit_begin` copies source bytes and reparses them into
an independent QPDF instance. Annotation and form entry tables retain QPDF
handles; public references encode a session cookie, a slot and a tag. Deleted
annotation entries remain tombstones. Form updates use a rollback journal, while
output snapshots serialize the editor without consuming it. The source and
earlier form snapshots remain unchanged, as tested by the mutation suites.

MED, fact: the cookie in `qpdf_document.cpp` mixes an atomic sequence with an
allocation address. It is an ephemeral session discriminator, not a persistent
graph identity. Neither its representation nor QPDF object numbers establish
identity for graph copies, independent editors or a rewritten PDF.

## Responsibilities and dependency direction

Introduce a private `src/graph` module for graph values, local identity, immutable
ownership and structural lookup. It must not include provider types or call a
parser. A backend adapter imports parsed values and maintains any mapping from
provider objects to graph IDs. `src/validation` consumes const graph values;
validation cannot repair or own the graph. The document facade coordinates
construction and publication, with `quantapdf_context` retaining runtime lifetime.

The graph represents catalog/trailer roots, page relationships, generic PDF
values, resources, metadata and stream payloads. QP IR represents interpreted
drawing content. A page-to-IR lowering step must explicitly interpret content
streams and inherited resources; merely storing those streams is not completion
of #145. Existing Composer IR continues to own its records until an actual
conversion has equivalent coverage. Do not create mirrored mutable resource
tables or cast a Composer resource index into an imported PDF object ID.

Catalog, ordered pages, resources and metadata are typed views/indexes derived
from graph nodes, not separate authoritative copies. Annotation/form/text/image
snapshots keep their current publication and borrow contracts. Optional CMeta
metadata may describe graph types later; it does not supply ownership, mutation
permission or parsing and is not required by this migration.

## Values, references and identity

Represent null, boolean, integer, real, name, byte string, array, dictionary and
stream distinctly. Names and strings carry explicit byte lengths. Retain exact
real tokens when conversion to a machine number would lose information; numeric
accessors must validate conversion. A stream owns its dictionary and its copied
payload representation, with an explicit encoding/filter contract. Do not label
decrypted encoded bytes as source-file bytes or silently decode/recompress them.
Structural inspection must not execute actions or invoke optional codecs.

Every referenced node has a graph-local ID. Zero is invalid. IDs are values, not
addresses, provider handles, PDF object numbers or positions in a compacting
vector. Indirect references preserve sharing and cycles. Inline values retain
their containment and must not be accidentally interned by equality. Dictionary
keys have a deterministic byte ordering; array/page order retains document order.

An ID is meaningful only in its owning lineage. Independent imports have
independent lineage owners even when their local IDs happen to be equal. Import
assigns local IDs through a specified root/key/array traversal, registering an
indirect object before visiting children. A backend-private map resolves repeated
objects. Reimporting the same admitted model under the same traversal yields the
same local ordering without incorporating process addresses or time.

Versions of one lineage preserve the IDs of surviving objects. Deleted objects
are tombstones, and committed IDs are never recycled. New nodes in distinct
transaction branches need distinct origin namespaces within that lineage; use
an origin identifier plus an append-only ordinal rather than letting two clones
allocate the same next ID. Exhaustion returns an error before publication.
Equal values are not necessarily the same object. Copying an object into another
lineage requires an explicit old-to-new mapping.

Keep graph identity separate from runtime reference validity. A public editor
reference remains restricted to its original editor and object family, even if
two editors share a base graph. A provider-independent reference helper can
allocate non-reused runtime scopes and validate slot/domain bounds, while the
owning table decides liveness. Runtime scope tokens are not serialized. The
current annotation/form reference structs and error behavior remain unchanged:
foreign/invalid references fail ARGUMENT; deleted annotations fail STATE.
Opaque references are not an authentication mechanism.

Deterministic output orders nodes and assigns output PDF object numbers using
document structure and writer policy, never runtime scopes, allocator addresses
or the order unrelated editors were created. This guarantees neither byte
identity to the original input nor canonical equivalence across arbitrary PDFs.
Those are separate contracts with separate tests.

## Construction, ownership and limits

Graph construction is unpublished and uniquely mutable. The builder owns every
allocated node, edge, key and payload until successful validation and sealing.
On failure it releases all partial storage, clears the output and leaves source
state unchanged. Sealing publishes an immutable owner; readers may borrow views
only while holding that owner. A builder pointer cannot survive growth or seal.
Stable IDs do not imply stable storage addresses or extend a borrow.

An immutable owner retains its context and its complete payload dependency
closure. It must survive destruction of the parser, source document and temporary
adapter maps. Final release destroys graph payloads before releasing the context,
consistent with #148. Existing image snapshots retain their documented source
lifetime restriction until their implementation and tests are explicitly migrated.

Require explicit internal import limits for node count, edge count, copied value
bytes, copied stream bytes and nesting/traversal work. Charge allocated capacity,
not only committed counts, and check multiplication/addition before allocation.
Adapter worklists/maps also need limits. These limits bound graph-owned storage
and adapter work; they do not pretend to bound all memory inside a third-party
parser. No limit may silently truncate the graph or publish a partial success.

Invalid internal arguments return ARGUMENT; invalid PDF structure follows the
existing FORMAT contract; unsupported representations or exceeded admission
limits return UNSUPPORTED; allocation failure returns NOMEM. Broken admitted
graph invariants return BACKEND with #149 reports. Preserve provider errors and
the existing precedence of public argument checks. A new graph budget cannot
silently become a stricter limit on an existing public API during migration.

Initial operations remain synchronous and externally serialized, including
retain/release and lineage-origin allocation. There is no async queue, concurrent
mutation or lock-free reclamation. A later concurrency proposal must specify its
own publication and shutdown protocol. No arena or pool is needed solely to
implement this design; #157 can evaluate storage strategies against these owners.

## Transaction model

An editor owns a retained immutable base version and a private change set. Reads
resolve the change set before the base; the change set is the only authority for
pending replacements/deletions. Indexes derive from that resolved view. New nodes
receive IDs in the editor's origin namespace. Shared base payloads remain const.

Each public mutation stages all fallible work before publishing its change set.
Failure preserves the preceding editor view, reference liveness and previously
published snapshots. Successful deletion records a tombstone instead of recycling
an ID. An aborted, unpublished allocation may be reclaimed, but any ID that has
escaped through a successful API result must never identify another object.

Snapshot/commit resolves and validates a complete candidate graph, then writes
output into unpublished storage. Only successful completion publishes the output
or new immutable version. Failure leaves the source, editor and earlier outputs
unchanged and permits retry. Snapshot does not consume or implicitly close an
editor; repeated snapshots preserve current public behavior. File replacement
continues through the established output-file publication path.

Do not maintain both a mutable QPDF tree and a mutable neutral graph as competing
state owners. Until a feature migrates, its current provider transaction remains
authoritative. After it migrates, a provider writer receives a resolved const
graph and builds temporary output state. A reader may not switch authority after
an error or silently fall back to a stale provider copy.

## Alternatives and rollout

Wrapping QPDF handles would retain provider-specific lifetimes and identities.
Eagerly rebuilding a graph on every open would add allocations and parser
admission failures to unrelated rendering calls. Replacing all providers at once
would combine graph import, rendering semantics, transactions and serialization
without a useful compatibility checkpoint. A second PDF parser or general
reflection framework is unnecessary. Select private, demand-driven import and
feature-by-feature migration with one state authority per operation.

1. Extract provider-independent local identity/reference rules with actual edit
   consumers and tests. Preserve deletion, repeated lookup, foreign-reference and
   domain behavior. This is only the runtime-reference portion of #156.
2. Implement the bounded graph owner/builder and QPDF import adapter behind private
   interfaces. Test shared references, cycles, roots, streams, deterministic
   traversal and independence from the parser. Feed structural rules into #149.
3. Migrate an existing read consumer only after its admission, error and snapshot
   behavior is covered. Expand catalog/page/resource/metadata views without
   shadow owners. #146 remains open until its actual required families work.
4. Introduce graph change sets and migrate form/annotation editing with the current
   failure-injection and immutability tests. Preserve isolated editor references.
   Writer coverage and equivalent output behavior are prerequisites for switching
   authority; isolated change-set unit tests do not complete #153.
5. Implement content interpretation and the relevant QP IR consumers for #145,
   with explicit origin/resource mappings and unsupported-feature admission.

Each step must leave usable internal code, a concrete consumer or formal boundary
test, and a named remaining scope. Do not expose incomplete public graph APIs.
There is no new dependency, public layout change or persisted graph format.
Reverting an individual step restores its previous private path; outputs already
published retain their established PDF semantics. Future persisted graph IDs or
incremental PDF writing require a separate versioned design and migration plan.

## First implementation: runtime references

The first #156 increment puts append-only local IDs, checked scope sequencing and
reference encoding/resolution in `src/graph/identity.h` and `identity.c`. It does
not yet implement graph nodes, graph versions or origin-qualified graph IDs.
The local ID is a nonzero 32-bit ordinal; zero denotes absence. Checked index
conversion accepts the last representable ordinal without wrapping.

Each editor obtains two distinct scopes from one non-recycling sequence, one for
annotations and one for forms. Scope creation is externally serialized under
the existing process-wide ABI v2 call contract; the C module does not introduce
a concurrent API. A scope is unique within the loaded runtime instance and is
not portable across runtime copies, processes or unload/reload. Exhausting the
64-bit sequence leaves it exhausted and returns NOMEM. Failed construction may
consume scopes; it never returns them for reuse.

Public references retain two opaque 64-bit words. The first is the scope and the
second contains a local ID and a derived check tag. Preserve the existing tag
mixing rule, but derive it in the common resolver instead of storing duplicate
tags in provider tables. No provider address contributes to identity. The public
opaque representation is not a persistence format or an authenticity guarantee.

The QPDF adapter still maps append-only slots to handles and checks annotation
tombstones after scope/tag/range admission. Existing discovery capacity limits
remain unchanged; annotation creation now checks the same ID limit and prepares
its reference before PDF mutation. Local IDs are independent of scope creation;
PDF output never contains runtime scopes or tags. This separation is ready for
the graph importer and transaction lineage integration described above, which
remain outstanding requirements of #156.

The module allocates nothing and owns no payload. It is compiled into the
existing private archive, preserving static SDK link closure. Tests compile the
same C source without provider libraries, exercise sequence/ID exhaustion and
reference failures, and use public edit APIs to verify lifetime, domain,
tombstone and output behavior. Reverting this increment restores private
reference bookkeeping with no persisted-data or public ABI migration.

## Imported graph implementation protocol

The first graph implementation adds a pure-C, uniquely owned model and builder.
Its flat node/edge/byte storage owns all copied values. Local IDs use the #156
ordinal; graph owners now attach runtime lineage/version identity independently
of this deterministic model. The private commit protocol is described below.
An internal document graph snapshot wraps the sealed model in the #148 snapshot
owner, retaining its context until all graph storage is disposed. Capturing is
explicit and demand-driven through the private backend dispatch table. Existing
public reads/edits retain their current owners and admission behavior.

Import uses deterministic breadth-first discovery: trailer first, dictionary
keys in unsigned byte order, arrays in source order. Register an indirect object
before scheduling its children; inline values get their own containment node.
Definitions follow increasing local IDs. Node edge ranges and byte ranges form
contiguous partitions, so storage validation scans each span once. Typed root
checks use binary dictionary lookup. Cycles and shared references are valid. The trailer/catalog
and ordered page IDs are explicit roots/views into the same nodes.

The builder reserves nodes, then defines each node once; child reservations may
occur during definition. Only one definition is active. Payload bytes precede
dictionary-key bytes, and each edge references an already reserved ID. Finish
temporarily installs roots/pages, runs the pure graph validator, and seals on
success. Failure rolls back root/page publication; allocated capacity stays
owned until retry or drop. A failed adapter import drops the complete builder
and leaves the output empty. Readers borrow const model data only after sealing.

Mandatory internal limits cover nodes, edges (including page-index entries),
value/key bytes, encoded stream bytes, total allocated model storage and first-
discovery depth. Total storage charges the owner and allocated array capacities;
it excludes allocator metadata and transient allocations internal to QPDF.
Adapter worklists and the indirect-object map are bounded by the node limit;
depth and edge limits bound discovery work. Page indexing also checks actual
page-tree depth, independently of graph discovery shortcuts. Growth caps itself to the remaining
storage budget instead of allocating speculative capacity beyond it. No budget
is silently inferred or added to an existing public API.

QPDF performs parsing/type admission. The adapter copies booleans, signed
integers, exact real tokens, canonical names/keys, and binary strings. Streams
use the success-reporting `pipeStreamData` overload with no decoding/encoding,
and a bounded sink writes directly into graph-owned storage. These are encoded
bytes after provider decryption, not original encrypted file bytes. Filters and
stream dictionaries are preserved for inspection. The private
[graph writer](2026-10-11-quantapdf-graph-writer-design.md) rejects encrypted
provenance, Crypt filters and external stream files until their policies exist.
Capture itself is not a lossless serializer. Warnings, unsupported values, traversal limits
and stream read failures abort capture rather than publish an incomplete graph.
The adapter uses the operation's success result, not its filtering-attempted
flag: [QPDF 12.4 sets that flag for empty streams too](https://github.com/qpdf/qpdf/blob/v12.4.0/libqpdf/QPDF_Stream.cc#L560-L588).

The adapter extracts ordered page IDs from the original `/Pages` and `/Kids`
hierarchy without invoking QPDF's repairing page helpers. An iterative traversal
admits only indirect `/Page` and `/Pages` dictionaries with array children;
repeated page-tree nodes (including cycles) fail with FORMAT. This restriction
does not apply to generic graph references. Traversal stacks and page IDs are
bounded by the imported node count and explicit limits. Resource inheritance
and absent resource entries remain exactly as parsed. Parent/count consistency,
page geometry, and other PDF semantic validation remain later validation work.

Validation checks storage partitions, all references, kinds, scalar and byte
shapes, dictionary order, roots and page node types with the #149 deterministic
first-failure report. It does not certify PDF/A/UA or interpret content streams.
The facade capture and formal parser-independent snapshot tests are the first
consumers. Migrating existing feature reads and public editing, completing graph
writer policies and QP IR lowering remain separate steps; their requirements are
not closed by capture or the private commits below.

## Graph identity across immutable commits

`quantapdf_graph_commit` in `graph_edit.h` copies a sealed base plus a batch of
whole-node replacements/additions into a separately owned sealed graph. The
[transaction design](2026-10-11-quantapdf-edit-transactions-design.md#immutable-graph-commits)
defines admission, root/page selection, failure publication and memory limits.
The graph-snapshot facade consumes this path and retains the context without
retaining parent payloads or provider state.

`quantapdf_graph_reference` contains lineage, creation-version and local ordinal.
A shared lineage owner issues non-recycled version numbers under external
serialization; ordered origin ranges identify nodes inherited from earlier
versions. Replacements keep their identities, while sibling additions at equal
local ordinals have distinct origins. These tokens do not enter the graph model
or determine traversal order. A reference resolves only where that exact origin
and ordinal still exist; it neither retains a graph nor authenticates callers.
There is no graph GC, physical node deletion, branch merge, persistence or public
editor-reference migration in this increment. Disconnected nodes remain owned.

The copy visits nodes, edges, payload bytes and provenance ranges linearly,
followed by structural and bounded page-tree/index validation. Reference lookup binary-searches
the origin ranges. Every range covers at least one inherited node, so provenance
is bounded by node count; no-op versions do not grow a retained history table.
Snapshot integration tests import real metadata, commit copied binary values,
drop the parser/base/context reference and inspect the surviving version. Pure
tests cover sibling rejection, inherited references, stale batches, cycles,
root/page changes, mismatched indexes, repeated tree nodes, page-tree depth,
bounded failure and deterministic model equality. Private graph-writer tests now
verify deterministic output, reordered page content/shared resources, binary
metadata, cycles and encoded streams after reopening. Graph-to-IR identity
mapping and broader output policies remain future work.

## Acceptance evidence

### Metadata read consumer

The private graph-snapshot metadata operation selects `/Info` and one field from
the sealed version through a pure-C query. Its borrowed byte view lives only as
long as that graph; the operation immediately converts it into independently
owned, NUL-terminated UTF-8 using the context's backend text decoder. Missing or
explicit-null entries return NULL/zero; an empty string returns allocated
empty/zero. Wrong Info/value types return FORMAT, unknown fields ARGUMENT, and
an unsealed graph STATE. Querying never mutates or retains a graph, and failure
resets the output. Calls remain synchronous and externally serialized.

The existing public document metadata implementation uses the same canonical
field mapping and decoder. QPDF remains responsible for PDFDocEncoding and UTF-16
conversion, including its established malformed-text behavior; a second Unicode
converter would risk changing that behavior. The graph query knows only graph
types and copied bytes. XMP precedence and date parsing are intentionally outside
the existing Info-field contract. Decoded output uses the existing malloc/free
ownership contract, so neither parser nor snapshot/context needs to remain live.

This is a read consumer for immutable graph versions, not a switch of public
document storage authority. Public metadata calls still read the source provider
without importing unrelated streams or applying new whole-document budgets.
All eight fields, missing/empty/error cases, committed versions, binary text and
detached lifetimes are compared against the existing public metadata operation.
Subsequent feature migration can consume the graph query without reintroducing
field maps or text converters. No public declaration, export or dependency changes.

### Remaining acceptance scope

Use backend-free tests for identity bounds, runtime scope exhaustion, deleted-ID
non-reuse, deterministic local ordering, graph ownership, capacity accounting,
publication failure and retained-version behavior. Backend integration tests must
exercise shared/cyclic indirect objects, direct containment, binary names/strings,
stream encoding, parser destruction, failed import and equivalent read results.

Transaction tests must cover independent editors, source/snapshot immutability,
reference domains, create/update/delete, allocation failure before publication,
validation/writer failure, retry and repeated deterministic output. Compare
logical graph identity separately from output PDF object numbering and ephemeral
public reference bytes. Existing fixtures and mutation tests remain compatibility
evidence; test-only success of an unused graph implementation is insufficient.

For each implementation step run focused CTest, the affected full native and
sanitizer suites, ABI checks, and shared/static SDK installation. This decision
document itself requires source/contract and link review, not an unrelated build.
The development presets instrument both first-party C and C++ sources, including
the importer. Prebuilt QPDF/PDFium and other third-party dependencies are outside
that instrumentation scope.
The Windows vcpkg QPDF archive has no MSVC STL sanitizer annotations. The Windows
development preset therefore disables the vector/string/optional annotations
consistently in first-party C++ objects while retaining AddressSanitizer's memory
instrumentation. This follows the [MSVC annotation compatibility contract](https://learn.microsoft.com/en-us/cpp/sanitizers/error-container-overflow).
Container logical-bound/lifetime annotation checks require rebuilding the full
C++ dependency closure with matching settings; this profile does not claim them.
