# IR resource registry

Implementation decision for [#147](https://github.com/qigao/QuantaPDF/issues/147),
following the owned IR in #151. The baseline is `59aaff1`.

## Evidence and decision

MED, fact: Composer currently grows seven resource arrays, implements separate
lookup/publication paths, and retains their capacities outside IR. `ir.c` frees
resource payloads, while two SVG rollback paths repeat part of that cleanup.
Fonts, forms, clips, graphics states, masks and paints already reuse IDs;
encoded and raw images currently always append a new record.

Place the seven typed arrays, committed counts and capacities in one
`quantapdf_resource_registry` owned by IR. Move semantic equality, checked growth,
publication, suffix rollback and final resource destruction to its module.
Composer remains responsible for public option validation, normalization, codec
admission and its overall resource-byte budget (which also includes operation
and navigation payloads). Backend export reads the registry through the IR.

Use typed find/intern functions, with the existing record types and one-based
per-kind IDs. No new generic type system, public handle, dependency, hash index
or global registry is needed. Linear lookup keeps first-insertion identity and
ordering deterministic; this work makes no performance claim. Equality compares
meaningful fields and payload bytes, never structure padding. Image identity
includes stored format, dimensions, components and alpha as well as normalized
bytes; different stored formats or color models are not guessed to be equivalent.

## Ownership protocol

The caller prepares a uniquely owned candidate. A valid intern call consumes
and clears that candidate on every result: it either publishes the record,
releases a duplicate and returns the existing ID, or releases it on admission/
allocation failure. The registry changes only on successful new publication.
Invalid call arguments do not transfer ownership. Separate find calls borrow a
candidate and permit existing font/gradient/clip preflight before payload copying.
Passing a published record as an owned candidate is forbidden.

Published payloads are immutable. Record-array addresses may change at append;
IDs and separately allocated payloads stay valid until rollback or disposal.
IR disposal destroys the registry. Moving IR transfers its registry with it and
does not retain the former Composer/context. No live checkpoint may cross a move.

SVG takes lexical registry checkpoints alongside its operation count and total
budget. Rewind frees only resources appended after that checkpoint, leaves
earlier/reused records intact, and keeps allocated capacity for reuse. Checkpoints
belong to one registry and one append lineage; reset, move or rewind past a mark
invalidates it. These are private preconditions, asserted in debug builds.
Operations referring to removed IDs are rolled back in the same transaction.

The existing externally serialized, synchronous execution contract remains.
There are no queues, blocking, asynchronous consumers or background cleanup.
Append, export, rewind and destruction require exclusive access and no live
borrow across mutation. Shutdown is ordinary IR/Composer destruction after use.

## Budgets, errors and compatibility

Intern computes retained payload cost with checked arithmetic and receives the
remaining Composer byte allowance. It reports zero newly charged bytes for reuse,
and the exact added payload bytes for a new record. Composer updates its single
total only after publication. Scalar resource records retain their existing
zero payload charge; count/array-size limits still reject overflow. Existing
resource array growth factors and ID widths remain unchanged.

Codec and raster preparation retain the existing remaining-budget checks. Thus
an image must still pass bounded normalization before registry reuse: a full
budget can reject even a duplicate image that requires decoding/copying. Register
once and draw the ID repeatedly remains the supported way to avoid that work.
This deliberately preserves the decoder working-memory boundary. Successfully
normalized identical images now reuse the first ID and do not charge payload
bytes twice. No hidden unbounded staging allocation is introduced.

Errors remain ARGUMENT, UNSUPPORTED for budget/size overflow, and NOMEM for
allocation failure. Failed publication clears the output ID and byte charge,
releases only the candidate, and preserves existing records. No new logs are
needed: callers continue to receive the status at the established API boundary.
Public declarations and exports stay unchanged; image ID reuse is the observable
addition requested by #147 and is documented and tested.

## Alternatives and verification

Merely grouping fields would leave ownership and rollback duplicated. A generic
tagged object store would replace existing typed IDs and complicate every writer
access. A hash index would add another structure to update during SVG rollback
without profiling evidence. The chosen typed registry centralizes actual
behavior while retaining existing data representations.

Migration updates Composer, IR, SVG rollback and the writer together. Reverting
the commit restores the former private arrays and repeated-image insertion; no
persisted data or public-ABI migration is required.

Formal tests cover every resource kind, stable IDs, meaningful-field inequality,
duplicate cleanup, exact byte charging, budget/overflow rejection, nested rewind,
post-rewind re-registration and disposal. Public tests cover copied image inputs,
deterministic output, the preserved normalization cap, existing resource reuse
and SVG rollback. Run focused registry/IR/Composer tests, then full Release and
sanitizer CTest suites and normal SDK installation, including Linux static CI.
