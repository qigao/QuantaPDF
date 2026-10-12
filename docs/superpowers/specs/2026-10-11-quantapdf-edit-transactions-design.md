# Edit transaction boundaries

Incremental implementation decision for [#153](https://github.com/qigao/QuantaPDF/issues/153)
on the runtime-regression baseline `cab0cd8`.

## Evidence and current scope

HIGH, fact: annotation update in `src/backend/qpdf_document.cpp` restores old
keys only in its explicit test-fault branch. An exception after a successful
key write bypasses that restoration. Annotation creation appends to `/Annots`
before adding the reference-table entry; failure in that later allocation can
leave an annotation without a published reference. Its explicit rollback also
leaves a newly created `/Annots` key behind. The form update journal handles
ordinary early exits, but silently ignores restoration exceptions.

The public edit facade already owns a separate provider session and retained
context. Beginning an edit copies source bytes and reparses them. Form and
annotation references name slots in that session. Snapshot writes a detached
output; it does not close the edit or mutate the source. These contracts remain
the compatibility baseline. This increment shares operation-level rollback
between existing form updates and annotation create/update. It does not add a
public commit API or claim that the provider is already a neutral edit graph.

## Transaction protocol

The provider session is the sole mutable fact source. A noncopyable local
transaction owns undo records and borrows that session for one synchronous call.
Before each covered mutation, it records the original dictionary key or the
original length of an append-only array. Dictionary records retain old values;
they do not authorize mutation of those old values or their children. Existing
array identity is preserved, including any aliases. Rollback reverses array
appends before restoring the containing key.

The lifecycle is prepare -> mutate -> commit, or prepare/mutate -> rollback.
Commit is nonthrowing and occurs after the last fallible operation, before
publishing a new reference. Successful rollback restores observable content and
leaves the session usable. An allocation exception still returns NOMEM; other
provider failures preserve their existing status mapping. Each operation has a
record-count bound derived from its write set: three annotation keys, a key plus
one array tail for creation, or two keys per form node/widget with checked size
arithmetic. Records last only for the call; there is no accumulating history.
Existing document/form limits remain authoritative. This is not a total heap
budget, and provider caches and unreferenced provisional objects may survive a
failed operation until session destruction.

Rollback is nonthrowing, attempts every saved record even after an error, and
marks the session unusable if any restoration fails. The initiating call keeps
its original error; subsequent valid reads, mutations and snapshots return STATE
with the usual cleared outputs. Dropping the session remains valid. Begin a new
edit from the immutable source to recover. This is a containment rule for an
uncertain provider state, not an attempt to publish partially restored data.

All calls and destruction remain externally serialized. There is one active
caller, no asynchronous consumer, cancellation, queue or drain. Published
outputs and typed snapshots own their payloads independently and remain valid
after a later edit failure. An output-write failure publishes no output and does
not commit or discard edits. Determinism means equal accepted edits and fixed
writer settings in the same dependency build; it is not a cross-version byte
format or an encryption randomness guarantee.

## Neutral graph and IR integration

The eventual internal edit graph will own an immutable base graph and ordered,
immutable change sets. Each change must identify its originating graph/version,
target local ID, expected prior state, owned replacement data and dependencies.
A candidate version must pass structural validation before publication; failed
validation/allocation leaves the preceding version authoritative. The document
graph can contain PDF cycles while the version/dependency history remains acyclic.

The `quantapdf_graph_model` has sealed local IDs; graph owners now attach runtime
origin/version identity as described below. Current QP IR represents composer
input rather than arbitrary PDF content. Therefore this change does not
fabricate graph-to-IR mappings, retain QPDF handles
in a pure-C graph, or force all edit inputs through graph-capture limits. Graph
identity work (#156), semantic graph editing (#146), arbitrary content IR (#145)
and ownership budgets (#157) must supply those contracts before migration.
Provider undo records are temporary implementation details, not serializable
change sets or a second authoritative model. Annotation deletion and snapshot
serialization retain their existing implementation; their broader failure
coverage remains part of #153/#152.

## Immutable graph commits

The next private graph increment implements whole-node replacement and append
as an atomic batch against an exact sealed base version. A change set borrows
input descriptors for one serialized call. Replacement IDs must be strictly
increasing and name existing nodes; additions receive consecutive local IDs
after the base nodes. Edges may refer to base nodes or any addition in that batch,
including forward references and cycles. The caller supplies replacement values,
never provider handles. Optional root/page replacement is explicit. Removing an
edge does not reclaim or recycle its target node; graph GC and public editor
tombstones are separate policies.

Each root graph owns a runtime lineage token and a small shared lineage owner.
That owner issues checked, non-reused version numbers to all branches. A node's
identity is `(lineage, creation-version, local-ID)`. Replacing its value preserves
identity. New nodes in sibling branches can occupy the same local slot but have
different creation versions. A compact ordered range table records inherited
creation versions; current additions share the candidate's version. All tokens
are ephemeral within one runtime instance and absent from the deterministic
node/edge/byte model. Cross-process persistence and mixing independent runtime
copies are unsupported.

Commit verifies the change set's lineage/version before allocating a candidate.
It copies nodes, edges, bytes and page indexes into a new bounded builder, copies
the inherited identity ranges, then validates and seals the candidate before
publishing it. Caller data, the base graph and its parser may be released after
success. Failure destroys the candidate, clears the output and leaves the base
usable; a reserved version number may be consumed but is never recycled. Stale
base selection returns STATE, invalid descriptors return ARGUMENT, budget limits
return UNSUPPORTED, and allocation failure returns NOMEM. Final structural
validation returns BACKEND and its deterministic report, as for graph finish.
Commit additionally verifies that the supplied page index exactly matches the
ordered leaves of the catalog's `/Pages` tree. Wrong tree types, direct tree
nodes, repeated nodes and cycles fail before publication. Generic object cycles
remain legal. `/Parent`, `/Count`, geometry and broader PDF semantics are not
validated by this increment.

Only the lineage owner is shared, with externally serialized checked retain and
release. Versions do not retain parent payloads or a history chain. Each graph's
storage budget conservatively charges the shared lineage owner as well as its
private owner, provenance ranges and node/edge/byte capacities. Thus summing
per-version storage may count lineage metadata more than once; it is not process
RSS or a bound on allocator metadata, caller input, the live base, or providers.
The complete base and candidate coexist during commit. No pooling, concurrency,
or reduction in allocation cost is claimed. Copying is bounded by node/edge/byte
limits. Page-tree validation also applies `max_depth`, counting both the root
and leaf on the active path. Its transient scratch is `node_count` bytes plus
`min(node_count, max_depth) * sizeof(quantapdf_validation_page_frame)` bytes,
allocated with checked sizes and freed before return. This bounded transient
cost is separate from persistent `storage_bytes` and its storage budget.

The private graph-snapshot facade is the first production consumer: a committed
snapshot retains the source snapshot's context and owns the new sealed graph.
Tests must use real imported PDF metadata as well as provider-free synthetic
models, drop the source snapshot/document/context, and inspect the surviving
committed graph. This still does not switch public metadata/form/annotation
authority. The subsequent private
[graph writer](2026-10-11-quantapdf-graph-writer-design.md) exercises deterministic
output, committed metadata and page order. Complete encrypted/signed output
policies and equivalent feature behavior remain required before that switch,
as do graph-to-IR mappings for #145/#156.

## Alternatives, migration and verification

Cloning and serializing the entire PDF per operation would change cost, provider
object identity and admission behavior. Copying an `/Annots` array and replacing
the page key would detach existing aliases. Keeping independent hand-written
rollback branches misses exception exits. The shared local undo owner preserves
current storage and reference semantics with a bounded per-operation record set.

No public declaration, export, persisted data or dependency changes. Reverting
this increment restores the old provider implementation without data migration.
The cost is a small record allocation for annotation mutations; no performance
improvement is claimed. A future graph migration must preserve reference scope,
input validation, failure/output publication and existing source isolation tests.

Extend the existing annotation mutation suite with exception unwinding after
writes, absent and non-array `/Annots` restoration, retry equivalence against a
clean session, and rollback-failure containment across annotation/form/output
entry points. Existing form rollback tests exercise the shared owner. Run focused
tests first, then the full Windows Release/ASan and Linux Release/fatal
ASan/UBSan CTest suites, ABI checks and SDK installation. Injected failures are
specific checkpoints, not exhaustive failure at every QPDF allocation.
