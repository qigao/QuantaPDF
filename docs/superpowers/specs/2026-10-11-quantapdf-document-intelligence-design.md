# Document intelligence extension boundary

Design proposal for [#176](https://github.com/qigao/QuantaPDF/issues/176), based
on `89cabe1`. This decision defines an optional consumer boundary; it does not
implement an intelligence provider, public plugin ABI, schema registry or index
engine. The existing native SDK remains usable without any such service.

## Review findings and current evidence

- **HIGH, fact and implication:**
  [`snapshot_owner.h`](../../../src/snapshot/snapshot_owner.h) defines uniquely
  owned snapshots whose borrowed views expire at drop.
  [`context_internal.h`](../../../src/core/context_internal.h) and the
  [SDK ownership contract](../../native-sdk.md#ownership-and-deterministic-cleanup)
  require external serialization, including destruction. Sending a copied native
  handle to a worker neither retains it nor permits concurrent SDK calls. An
  adapter must own the handle until use ends, or copy the selected data before
  dispatching external work.
- **HIGH, fact and implication:**
  [`graph.h`](../../../src/graph/graph.h) identifies objects by runtime lineage,
  origin and local ID. References survive value replacement in descendants;
  they do not identify unchanged content or a persistent document revision.
  Caching by object reference alone can attach stale embeddings or semantic
  claims to a later value. Bind results to the selected snapshot version and
  extraction provenance, with separate durable identity for persisted results.
- **MED, fact:**
  [`structured_text.c`](../../../src/snapshot/structured_text.c) publishes copied
  blocks, lines, spans and UTF-8 views. The
  [structured-text test](../../../tests/test_structured_text.c) reads them after
  dropping the page and document. This supports external analysis today; it
  supplies geometric observations, not an OCR engine, semantic reading order,
  a tagged-document interpretation or PDF-object-to-text-span identity mapping.
- **MED, fact:**
  [`graph_snapshot.h`](../../../src/snapshot/graph_snapshot.h) and
  [`graph_query.h`](../../../src/graph/graph_query.h) supply private bounded
  capture and deterministic queries. They are not installed extension APIs.
  [`document_backend.h`](../../../src/backend/document_backend.h) explicitly
  defines trusted private dispatch, not a plugin ABI. No current public schema,
  provider registration or document intelligence API exists. Reusing these
  models must not expose private structure layouts as a stable SDK contract.

## Decision and ownership of responsibilities

Keep inference, layout classification, embeddings and search services above the
native PDF kernel. A host-selected adapter projects existing immutable
observations into a bounded input batch. The provider returns a separate derived
result. PDF parsing, native extraction, graph ownership, edits and serialization
remain in their existing modules.

```text
Native SDK / immutable observations
    | read and copy under existing SDK serialization
    v
Host adapter: revision, selection, projection and admission
    | owned input batch
    v
Explicitly selected external provider
    | derived result with provenance
    v
Host validation and publication -> sidecar / index / application view
```

The dependency points from the adapter to QuantaPDF. The kernel does not import
an inference SDK, model loader, embedding store, network client or application
service locator. `quantapdf_context` remains the existing native lifetime domain;
it does not become the owner of models, remote credentials or service workers.
Provider selection is explicit host configuration, never PDF metadata.

| Data or operation | Authority | Extension behavior |
| --- | --- | --- |
| PDF bytes, graph values, Info fields | Native document or selected immutable graph version | Observe through the supported projection; preserve source values. |
| Extracted text and geometry | Completed native snapshot | Copy exact selected observations and record extraction provenance. |
| OCR text, layout labels, summaries, entities, embeddings | External provider result | Publish as derived data with producer identity and source anchors. |
| Index entries and retrieval order | Index service under #163 | Derive from identified inputs; never become the PDF source of truth. |
| Applying an annotation or metadata change | Existing explicit edit workflow | Treat a provider suggestion as input requiring a separate application decision. |

## Inputs, schemas and source anchors

There are two integration stages, with the same ownership rule:

1. **Available today:** an external adapter uses installed APIs such as
   `quantapdf_extract_structured_text`, the text accessors,
   `quantapdf_document_metadata`, page geometry and explicitly requested
   rendering. It traverses block/line/span ordinals in accessor order, preserves
   byte lengths and copies the selected data. No private headers are required.
2. **Future native integration:** after #158/#162 define an admitted extension
   projection, its implementation consumes the existing graph query and
   snapshot models internally. It must not build a second object graph or
   depend on QPDF/PDFium handles. The private graph cursor and node layouts
   remain private. This stage requires implementation and compatibility tests
   before any external provider can use it.

The logical input/result envelope belongs to
[#162](https://github.com/qigao/QuantaPDF/issues/162); this document establishes
its required information, not a wire format or new C structure:

- Envelope schema identity/version, operation and requested output kind.
- Source document revision, selection, extraction implementation/version and
  effective options, plus a digest of the exact projected input for reuse.
- Ordered page and block/line/span anchors with explicit text byte ranges.
  UTF-8 byte offsets, codepoint ordinals and model token offsets are distinct;
  any normalization/tokenization records its mapping or leaves the finer anchor
  unavailable. Embedded NUL and empty values follow explicit lengths.
- Geometry in the existing
  [displayed page space](../../../README.md#page-coordinates), including its
  units and source page bounds. An OCR projection additionally identifies the
  rendered image and the transform between pixels and that page space.
- For a graph-backed projection, the selected graph version as well as object
  references. Page ordinal is version-local and is not persistent page identity.
- For derived output, producer/model revision, effective preprocessing and
  inference settings, input identity, result kind and any available confidence
  interpretation. A score without a specified meaning is not a probability.

Reuse the object, resource and snapshot descriptions from #162. A future
[#155](https://github.com/qigao/QuantaPDF/issues/155) CMeta adapter describes those
same projections; it does not own their data or grant write/execute permission.
Do not introduce a parallel reflection model or require Salts/CMeta to consume
the native SDK. Derived fields live in an extension namespace, separate from
the source Info dictionary or native validation report.

Runtime graph references must not be serialized as durable cache keys. A host
can assign an immutable source revision to its exact input bytes and retain
that association; reopened data requires the host's durable revision protocol.
Runtime lineage/version is useful only while that graph lineage exists. An
index may keep copied anchors without retaining the native snapshot, but can
resolve them later only against the matching revision and extraction contract.

The existing public text snapshot has no automatic mapping to an independently
committed graph. Pair public text and graph observations only when the adapter
establishes that they describe the same source revision. A graph edit can change
content, resources, geometry or page order. Until graph-to-IR extraction exists,
analysis of such a version needs an explicitly admitted serialize/reopen path
and new provenance, or rejection of that requested input. The current graph
writer's [admission policy](2026-10-11-quantapdf-graph-writer-design.md) still
applies. Do not reuse source-document text merely because a page ID survived.

## Provider admission and native ABI

An intelligence provider is a runtime service consumer, not an alternative PDF
parser/renderer. Its operation contract declares accepted schema versions,
input kinds, supported output kinds and limits before processing. A host selects
one provider for a request and validates compatibility before transferring data.
An unavailable capability fails that request explicitly; ordinary open, render,
extract and edit calls continue to work without the service.

[#158](https://github.com/qigao/QuantaPDF/issues/158) and
[#172](https://github.com/qigao/QuantaPDF/issues/172) own any future native
registration, capability negotiation and versioned callback surface. Do not
append intelligence callbacks to the private backend table and treat it as an
external ABI. Public declarations, status enums, export lists, package targets
and default dependencies stay unchanged in this decision. An external host can
integrate a provider using today's public extraction APIs without registration
inside QuantaPDF.

In-process providers are trusted application code. Native handles, backend
pointers and callback addresses do not cross a process/network boundary.
Out-of-process services receive only admitted serialized projections; transport,
isolation and credentials belong to the host and
[#167](https://github.com/qigao/QuantaPDF/issues/167). A remote provider receives
only the selected content explicitly submitted by the host. Document text and
embedded instructions are input data, not authority to select tools, run actions
or alter the host's access policy.

## Lifetime, capacity, concurrency and failure

The initial adapter protocol has one owner per job and one active provider call
per admitted job. Preparation and native destruction occur under ABI v2's
process-wide external serialization. The adapter copies a complete selected
batch before dispatch; external compute may then run concurrently without
calling QuantaPDF or accessing native borrows. It must not hold the native
serialization lock during model execution or remote I/O.

Snapshot ownership is unique. The preparation owner drops a snapshot only after
its last copy; it never invents a snapshot retain by copying a handle. Image
snapshots and bitmaps retain their documented source-document lifetime
requirement. Copy needed pixels while that document is still live. A future
native projection that transfers a snapshot must specify a real transfer and
exact drop obligation separately; it cannot silently change this copy protocol.

The host configures finite admission limits for selected pages, copied text and
pixel bytes, input/output records, result bytes, embedding dimensions, active
jobs and queued bytes. The adapter checks multiplication, addition, ranges and
capability limits before committing allocations or publication. Charge actual
reserved capacity and all simultaneously owned input/result batches; do not
count only logical payload lengths. Queue admission transfers batch ownership
only on success. On rejection the submitting owner still releases the batch.
There is no implicit unbounded queue, truncation or automatic model fallback.

These adapter budgets do not retroactively bound provider parsing, native text
extraction, GPU memory or remote service memory. The existing public extraction
API has no caller-supplied allocation budget. A hard process-memory/deadline
requirement therefore needs separately enforced worker isolation; limiting a
copied projection alone cannot satisfy it. Private graph capture retains its
own explicit node, edge, byte, storage and depth limits.

Each job moves from preparation to admitted execution, then validation and one
terminal publication or failure/cancellation. Native failures preserve their
existing `quantapdf_status`; provider/schema/budget/timeout/cancellation outcomes
remain distinct in the host's service contract. No new native error code is
implied here. Failure releases private partial results and publishes no complete
result. Empty successful extraction is distinguishable from failed extraction.
Results arriving after cancellation or supersession cannot replace the selected
revision's published result.

Before publication, the host checks the result's schema and requested kind,
source revision, selected-page anchors, byte ranges and record/byte budgets.
Embedding count, dimensions and numeric representation must match the admitted
capability; nonfinite components are rejected. Normalized text requires its
declared source mapping. Invalid or mismatched output fails the job rather than
being attached to a nearby page, silently repaired or accepted as partial success.

Cancellation stops publication and requests cooperative provider termination;
it does not free buffers while a provider may still read them. Shutdown stops
admission, releases queued batches, cancels active jobs, waits for acknowledged
completion, then releases results, provider instances and their code. A provider
that cannot stop may require process termination under #167; timeout alone never
proves quiescence or permits unloading an in-use module. Progress, counters and
failure diagnostics belong to the host's service operation and #170; document
text, passwords and credentials are not diagnostic payloads.

## Rebuilds and publication

[#163](https://github.com/qigao/QuantaPDF/issues/163) owns indexing. Rebuild input
order follows the selected immutable projection: graph local-ID/page/member
order or native text block/line/span order. Models may derive another reading
order, but must preserve source anchors and label the derived relation. Repeated
external results cannot be assumed byte-identical merely because graph traversal
is deterministic.

Reuse requires the exact input projection, schema, selection, producer/model
revision and effective settings. Replacing a graph value invalidates derived
data even when its object reference still resolves. A document edit initially
invalidates the affected revision's derived results conservatively; finer
dependency invalidation requires evidence and tests in #163/#166. Staged index
publication names the source revision and makes a complete generation visible
atomically. Cancelled, malformed or obsolete results never masquerade as a
successful current generation. Provider side effects such as billing are not
rolled back by native transaction semantics; retry policy belongs to the host.

## Alternatives, rollout and validation

Putting AI calls in extraction would add service failures, allocation and
network dependencies to stable native APIs. Exposing backend or graph layouts
would tie extensions to private versions and borrowed lifetimes. Building a new
object/reflection model would duplicate graph, schema and query authority.
Choose an external consumer and explicit projection instead. Copying selected
data costs memory, but gives independent lifetime and bounded retention; no
performance benefit is claimed without measurement.

This documentation increment requires no SDK migration or data conversion.
External host integrations can begin with today's public observations. Native
extension projections depend on #158/#162 admission and compatibility work;
index integration additionally depends on #163. Graph-version text analysis
needs the provenance/conversion path above. Each implementation increment must
name a real consumer and supported capabilities before adding an API. Rollback
removes the opt-in adapter or selects no service, leaving native calls and source
PDFs intact. Derived storage cleanup follows its host-owned lifecycle; it never
deletes or rewrites the source document.

The current evidence is the existing structured-text, context, graph snapshot,
graph edit and graph import suites. They verify native observations and ownership,
not an unimplemented provider pipeline. Future implementation acceptance must
cover detached/cancelled jobs, exact-capacity and overflow rejection, output
validation, wrong schema, unsupported capability, stale revision and sibling
reference rejection, edited/reordered-page anchors, NUL/UTF-8 range handling,
pixel transforms, deterministic projection rebuilds and provider failure without
source mutation. Native ABI/export and optional-dependency checks must continue
to pass with no intelligence provider installed. Use the existing test framework
and CTest; this design-only change needs source/link/diff review, not new test
infrastructure or an unrelated rebuild.
