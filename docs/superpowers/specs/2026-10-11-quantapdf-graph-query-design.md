# Deterministic private graph queries

The graph now supports detached metadata reads, but pages, objects and resources
still require callers to know array offsets. For #159, add a pure-C query layer
over the immutable graph, using the stable references from #156. The metadata
consumer uses the same exact-member operation rather than a second lookup path.
Public C ABI and document-provider authority remain unchanged.

## Model and ownership

A cursor borrows one sealed graph and stores only its enumeration position.
Objects are enumerated by ascending local ID, including disconnected stored
nodes; pages follow the admitted page index; members follow array order or the
graph's strict unsigned-byte dictionary-key order. These are finite enumerations,
not recursive reachability walks, so shared references and cycles are harmless.

Each result carries a stable value reference, a borrowed node/type view, and
member key bytes/owner/ordinal when applicable. Keys have explicit lengths.
References contain no provider handles or descriptor addresses. Consumers can
bookmark by copying a cursor or stop by discarding it. The graph must remain live
until all cursor and node/key views are discarded. Retained value references may
later resolve in a compatible descendant, while sibling additions are rejected.
There is no allocation, mutation, callback, cache, queue or hidden retained owner.
Calls follow the graph's existing external serialization contract.

Initialization and next/lookup operations reset outputs. Exhaustion/missing keys
return OK with found=false; explicit null members remain found=true. Bad arguments
or foreign references return ARGUMENT; unsealed/uninitialized state returns
STATE. Iteration advances only after publishing a complete match. Consumers must
not alter cursor implementation fields. Key/reference lookup accepts canonical
names, not a query language, path parser or implicit coercion.

## Page resource inheritance

The resource query returns the whole nearest `/Resources` dictionary from a page
or its `/Pages` ancestors. Missing/null resources inherit; an empty dictionary
stops inheritance. Categories and named resources are enumerated with ordinary
member cursors, preserving shared resource identities. The match owner identifies
the page or ancestor supplying the dictionary. No dictionary merge or QPDF repair
is performed. This follows the existing provider's documented inheritable
attribute boundary in `QPDFPageObjectHelper::getAttribute`.

The caller supplies a nonzero parent-step budget, including the initial page.
Encountered wrong types return FORMAT; a consumed caller budget returns
UNSUPPORTED. Traversal is also capped at the graph node count: a remaining parent
after that many dictionaries proves a cycle and returns FORMAT. Queries stop at
the first valid resource dictionary and do not certify unrelated parent links,
`/Count`, page geometry or the whole page tree. These rules are separate from
the graph validator's structural certification.

## Alternatives and verification

Materializing query-result snapshots would allocate and introduce new retention
costs without serving the current synchronous readers. Recursive visitors require
visited/work storage and ambiguous stop behavior. Borrowed finite cursors reuse
the immutable graph and expose deterministic ordering with explicit lifetimes.
Reachability predicates, sorting, indexing and schema/CMeta descriptors can be
added as separate consumers without changing stored identity or provider ABI.

Formal tests cover object/page/member order, cursor bookmarks and exhaustion,
cycles, explicit null keys, binary names, inherited/local/empty/malformed resource
dictionaries, parent budgets/cycles, sibling and descendant references, and the
existing metadata query through this common layer. Provider integration tests
compare inherited resource selection with QPDF while avoiding repair operations.
Public feature migration, general query expressions and CMeta integration remain
separate requirements; this increment implements the private typed query layer.
