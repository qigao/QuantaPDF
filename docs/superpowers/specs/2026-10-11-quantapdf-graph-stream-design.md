# Decoded streams from immutable graph versions

Implementation step toward [#145](https://github.com/qigao/QuantaPDF/issues/145),
using #146/#156/#159 graph ownership and references. This provides bounded stream
input for a later content interpreter; it does not interpret drawing operators,
map fonts/images to IR resources or complete arbitrary PDF-to-IR import.

## Evidence and decision

The graph importer owns encoded stream payloads after provider decryption, with
their original filter dictionaries. The graph writer already reconstructs QPDF
values using indirect placeholders and bounded, iterative direct-value ordering.
Duplicating that reconstruction for content interpretation would create another
identity, cycle and direct-expansion policy.

Extract that existing replay into a private C++ helper used by the writer and a
stream reader. It copies values into a temporary QPDF instance and returns a
local-ID-indexed handle table. It neither attaches a trailer nor applies output
security policy. Trailer bookkeeping, encryption/signature admission and PDF
serialization stay in the writer. Replay preserves sharing, direct containment
and encoded payloads; it performs no decoding or external file access.

The snapshot stream facade resolves a stable reference against its selected
immutable graph, then dispatches to the context's stream decoder. A replacement
in a descendant resolves to that descendant's bytes. Foreign references, references
to values introduced only in a sibling, and non-stream values fail ARGUMENT;
an unsealed graph fails STATE. The public
C ABI, source-document APIs and exported symbols remain unchanged.

## Decode and publication protocol

Inputs are borrowed synchronously. A temporary provider model copies the entire
graph under the same explicit direct-depth/expanded-visit admission used by the
writer. The depth cap remains 128. This bounds traversal, not all provider heap
storage; replay also allocates tables proportional to the already bounded graph
and copies its payloads. A future selective replay may reduce that cost but must
preserve identity and lifetime rules. This implementation does not claim a
per-stream memory cost independent of the source graph.

Only the selected stream is decoded. The provider's non-lossy generalized and
specialized filter capability is queried before delivery. Unsupported codecs or
parameters fail UNSUPPORTED; malformed filter metadata/warnings fail FORMAT.
External-file stream keys and explicit Crypt filters are rejected. Already
authenticated, decrypted graph payloads may be inspected; this grants no new
PDF write/decryption capability and does not change the writer's encrypted-source
policy. Lossy image decoding and inline images belong to later IR/resource work.

Use QPDF's success-reporting `pipeStreamData` overload, with no normalization,
recompression or raw-data retry. Its
[null-pipeline capability check](https://github.com/qpdf/qpdf/blob/v12.4.0/libqpdf/QPDF_Stream.cc#L596)
has a distinct result from an actual delivery: a successful unfilterable stream
copy is not proof of decoding. Both phases must succeed without warnings before
publication. Partial bytes remain private and are discarded on failure.

A malloc-owned sink grows only within the explicit decoded-byte limit, checking
remaining capacity before arithmetic and allocation. The limit charges allocated
output capacity, including a codec's large output chunk. Zero limits are invalid.
Budget exhaustion is UNSUPPORTED; allocation failure is NOMEM. These limits do
not bound every codec workspace or CPU time. No unbounded getStreamData buffer
is used as an intermediate decoded result.

Only a finished successful sink publishes its detached byte buffer. Empty streams
succeed with NULL/zero; other successful results use `quantapdf_free`. Failure
also resets outputs, so callers must check status. Output bytes survive source
document, parser, graph snapshot and context destruction. Replay handles and
temporary pipelines are destroyed before return. No retained provider map,
decoded-byte cache, graph mutation or background job is introduced; current
external serialization applies.

## Compatibility and validation

The writer continues to consume the same reconstructed values in the same order,
with the same trailer policy, deterministic ID and stream-preservation settings.
Its existing value/cycle, byte-determinism, source-ID and round-trip tests are
required regression evidence. Reverting this increment returns replay to the
writer and removes the private read operation; there is no persisted-data or
public API migration.

Formal tests cover unfiltered/empty/binary data, lossless filter chains and
predictors, malformed metadata/data, unsupported codecs, explicit Crypt/external
file rejection, exact/insufficient budgets, output reset and reference origin.
An in-memory AES-256 fixture proves authenticated binary stream decoding after
both the source parser and encrypted file buffer have been destroyed.
Versioned stream replacement and detached snapshot tests prove that decoding
uses the selected graph rather than a stale source parser. Run focused graph
read/write tests, the complete native Release/sanitizer suites and SDK installs;
Linux CI checks static link closure and fatal ASan/UBSan behavior.
