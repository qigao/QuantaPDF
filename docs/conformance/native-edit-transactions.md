# Native annotation transaction fault acceptance (#153)

Annotation deletion uses the existing edit transaction journal. All matching
/Annots positions and handles are retained before mutation; rollback restores
the same array identity in reverse mutation order. The runtime reference is
tombstoned only after success. Allocation failure maps to NOMEM; failed
restoration makes the edit session unusable, matching create/update.

The existing annotation mutation executable runs its transaction selection in
the native suite. Cases cover first/middle/last erasure of three interleaved
references, a shared indirect /Annots across two pages, pre-mutation NOMEM,
post-mutation NOMEM, exact before/after serialized bytes, same-session retry,
retained snapshots and rollback poisoning. Snapshot faults use an output
Pipeline that throws on its second actual serializer write, separately from
the old before-publication hook. Valid retry must reproduce full bytes.

These per-session private hooks do not add public ABI exports. Source remains
immutable. Broader Graph migration and signed/encrypted writer policy remain
open under #153; this increment does not close that issue.
