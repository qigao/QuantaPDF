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

The current `quantapdf_graph_model` has sealed local IDs but no origin/version
mapping, and current QP IR represents composer input rather than arbitrary PDF
content. Therefore this change does not fabricate mappings, retain QPDF handles
in a pure-C graph, or force all edit inputs through graph-capture limits. Graph
identity work (#156), semantic graph editing (#146), arbitrary content IR (#145)
and ownership budgets (#157) must supply those contracts before migration.
Provider undo records are temporary implementation details, not serializable
change sets or a second authoritative model. Annotation deletion and snapshot
serialization retain their existing implementation; their broader failure
coverage remains part of #153/#152.

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
