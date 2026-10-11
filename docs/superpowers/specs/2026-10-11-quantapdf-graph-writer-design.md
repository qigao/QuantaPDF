# Private Document Graph writer

## Boundary and choice

The immutable commit path produces detached graphs but cannot yet generate PDF
output (#146/#153/#156). Add a private snapshot-to-output operation through the
context's existing backend table. Reconstruct a fresh QPDF from graph values;
do not retain or mutate the source parser, reopen source bytes, or introduce a
second PDF serializer. Public feature migration remains a later change.

The graph also owns the source header version and whether capture decrypted an
encrypted document. Commits inherit these facts, even if a batch removes the
trailer encryption dictionary. Unknown version is allowed for synthetic builder
tests but cannot be written. Catalog version/extensions remain ordinary graph
values; the writer retains them and sets the source header as its minimum.

The first private writer rejects encrypted provenance or an encryption dictionary,
explicit Crypt filters, external-file streams, and signature dictionaries/fields.
It must not silently decrypt output, preserve stale signatures, or dereference
external stream files. Supporting these requires explicit encryption/signature
policies before public migration. This is not a byte-preserving or incremental
writer: xref offsets, stream lengths, document IDs and object numbers are writer
owned. Trailer xref bookkeeping is omitted; ordinary catalog and trailer values
are copied. Encoded stream bytes and ordinary filter dictionaries are preserved.

## Ownership and bounded work

One externally serialized call borrows the sealed graph. It validates page-index
agreement, then constructs a fresh provider document using indirect placeholders
and an iterative postorder for direct values. Indirect cycles remain legal;
direct containment cycles cannot be serialized and are rejected. All temporary
owners unwind on exceptions. Publication transfers malloc-owned bytes to the
existing output owner, which retains the context and outlives the snapshot.

The write limits bound output bytes, direct nesting and expanded direct-value
visits. The adapter caps direct nesting at 128 because QPDF recursively emits
direct objects. DAG expansion is calculated with checked additions before
provider reconstruction, including repeated direct children; exhaustion rejects
the call. Work arrays are bounded by the admitted graph node count. Provider
object storage and allocator overhead are separate from graph storage accounting.
The output Pipeline grows only within the byte budget, rejects overflow before
copying and frees unpublished storage. No queues, pooling or concurrency are added.

The existing output allocator is moved to `core/output_lifetime.c` so both the
library facade and boundary tests use the same private allocation/retain path
without compiling duplicate public output functions. Allocation provenance and
the public free/drop contract are unchanged.

Failures reset outputs: ARGUMENT for invalid limits, STATE for unsealed owners,
UNSUPPORTED for policy/budget limits, NOMEM for allocation failure, and BACKEND
for invalid serialization structure/provider failure. The base and existing
outputs remain usable. No automatic retry or fallback writer is introduced.

The preflight admits serialization shape and page-tree/index agreement; it does
not validate all PDF semantics, page parent/count consistency, content operators
or codec payloads. Source imports and provider warnings still fail according to
their owners. No PDF/A, PDF/UA or byte-for-byte lossless claim is made.

## Validation and rollout

Formal tests must capture, commit, write, reopen and compare metadata/page order,
binary values, shared/cyclic references, filters and encoded streams. They must
also cover deterministic repeated output, version preservation, direct cycles,
depth/expansion/output limits, retry and detached output/context lifetimes.
Encrypted provenance survives commits and rejects writing even if /Encrypt is
removed. Existing public suites remain the compatibility gate. Keep the operation
private until encrypted/signed input policy and feature migrations are complete;
rollback removes the private dispatch/caller without changing the public ABI.
