# QuantaPDF document intelligence extension boundary

Tracks [#176](https://github.com/qigao/QuantaPDF/issues/176).

## Goal and delivery scope

Define how an external host can derive layout, semantic metadata, embeddings,
and indexes from QuantaPDF observations while preserving the native PDF kernel.
This change delivers the boundary design. It adds no SDK functions, provider
runtime, wire format, model dependency, or persistence format.

The contracts below are requirements for a future host adapter. They do not
describe an implemented QuantaPDF intelligence service.

## Current evidence

- The [public header](../../../include/quantapdf/quantapdf.h) exposes opaque
  handles, structured-text accessors, and geometric text search. Backend types
  remain private.
- [Structured-text extraction](../../../src/structured_text.c) produces an
  owned snapshot; span text returned by its accessors borrows that snapshot.
  [The lifecycle test](../../../tests/test_structured_text.c) reads the snapshot
  after closing its source document.
- [Text search](../../../src/search.c) operates on that snapshot. It is a
  UTF-8 text search operation, rather than the proposed document graph query
  engine. [Its test](../../../tests/test_text_search.c) also closes the document
  before searching.
- [PDFium extraction](../../../src/backend/pdfium_document.cpp) currently
  groups nonempty extracted text into one block. These observations do not
  establish semantic paragraphs, multi-column reading order, tables, or OCR.
- The [v2 threading contract](2026-09-01-quantapdf-v2-api-stability-design.md)
  requires external serialization of all QuantaPDF API calls, including
  snapshot accessors and calls involving different documents.
- [The build](../../../CMakeLists.txt) links the private PDFium/qpdf adapters.
  It currently has no CMeta, Plugin, or intelligence-provider dependency.

## Placement and dependency direction

```text
Application / external service host
  |
  +-- SDK access owner -- public QuantaPDF C11 API -- private PDF backends
  |       |
  |       +-- copied observations
  |
  +-- immutable host input -- selected intelligence provider
  |                                |
  +-- validated derived results <--+
          |
          +-- application presentation / host-owned index
```

The host depends on the SDK. The SDK has no reverse dependency on the host,
provider, model, network client, index store, or plugin loader. Provider
discovery, configuration, scheduling, and storage belong to the host.

Intelligence providers consume independently owned observations. They receive
no document, page, bitmap, or snapshot handle, private backend pointer, or SDK
callback. Host extraction finishes before provider invocation, and provider
callbacks run outside the SDK serialization guard.

This extension is distinct from replacing a PDF backend in
[#158](https://github.com/qigao/QuantaPDF/issues/158). A derived layout result
does not become a parser result, and a semantic annotation does not mutate the
PDF. Applying a derived suggestion to a document is a separate, explicitly
requested edit through the existing edit/transform API.

## Host input and source references

The first adapter uses existing structured-text accessors, preserving their
block/line/span traversal order and explicit UTF-8 byte lengths. Geometry uses
the existing displayed page space: CropBox top-left origin, x right, y down,
and points before render scaling. Provider reading order is a derived result.

Every request carries an immutable host input with:

| Content | Required meaning |
| --- | --- |
| Input version | Version of the host observation contract, separate from SDK ABI and provider version |
| Request token | Host-issued identity unique to this invocation; retries and repeated operations use new tokens |
| Source token | Host-issued identity for one immutable source revision; never an address or file path alone |
| Snapshot token | Host-issued identity for this captured observation set |
| Page selection | Explicit source page indices and displayed bounds; no implicit selection of all pages |
| Text observations | Copied span bytes, traversal indices, and available public block/line/span information |
| Requested operation | Explicit layout, semantic, or embedding operation and its parameters |
| Producer provenance | SDK version and extraction/normalization policy used to create the observations |

Reference a text range by source token, snapshot token, page index,
block/line/span indices, and a half-open UTF-8 byte range within the copied
span. Check all indices, byte boundaries, and arithmetic against the captured
input. Cross-span evidence is a list of such ranges. A reference is valid only
for that snapshot; it is not a PDF object ID or a durable identity across edits.

The host issues a new source token after a transform or edit creates different
source content. File replacement at the same path cannot reuse a source token.
Persistent identity and cross-revision mapping require the separate contract in
[#156](https://github.com/qigao/QuantaPDF/issues/156).

Image-based analysis is an explicit later input profile. It must specify pixel
format, stride, dimensions, rendered region, and the mapping back to page
space. It cannot assume rendered pixels have the same units as text geometry.
The initial text profile does not implicitly rasterize or perform OCR.

## Ownership, execution, and capacity

The host has one SDK access owner and routes every SDK call through its common
serialization mechanism. This includes extraction, snapshot access, and
cleanup; a per-document lock is insufficient for ABI v2.

The access owner checks each SDK status before consuming outputs, copies the
selected observations, then drops snapshots through their matching SDK drop
functions. Allocator-matched `char **` outputs use `quantapdf_free()`. Only
fully initialized host storage can be published to a provider.

One request has one selected provider consumer. Fan-out creates explicit
separate requests. Immutable host storage remains alive until invocation and
all agreed cleanup complete; SDK pointer retention is never used to implement
that lifetime. Provider concurrency is permitted only when its own contract
supports it, and workers operate exclusively on host data.

The host configures positive hard limits for selected pages, input bytes,
result bytes, result records, pending requests, and in-flight requests. It
checks count/size arithmetic before allocation and rejects requests that exceed
a limit. A full queue rejects admission immediately; it neither silently drops
requests nor expands without a bound. There is no required global completion
order between independent requests.

Per-request limits are paired with aggregate budgets for retained input and
staged result bytes across queued and running requests. The host reserves budget
before copying or admitting work, and releases it exactly once after the last
use. Allocation failure rolls back the reservation. The result store has its
own capacity/admission policy; reaching it fails publication rather than
implicitly deleting user results or growing without a bound.

These are adapter retention/admission limits. Existing SDK extraction may
allocate a snapshot before its size is known; this design does not claim a hard
bound on parser memory or CPU. Enforced parser isolation and resource budgets
belong to [#167](https://github.com/qigao/QuantaPDF/issues/167).

## Provider contract and result publication

Before admission, the host verifies operation support, input/result contract
versions, required capabilities, and configured limits. An unsupported
operation returns an explicit failure. Providers are selected explicitly;
there is no automatic fallback to another model, remote service, or OCR path.

A successful result contains the request/source/snapshot tokens, operation, provider
identity and revision, model/configuration revision where applicable, and
source references for evidence. Layout and semantic records are derived data.
Embeddings additionally identify their model revision and vector dimension;
vectors from incompatible models cannot share an index namespace.

The host validates versions, operation, provenance, sizes, references, and
numeric fields before publication. Geometry and vector values must be finite;
coordinates must follow the agreed page-space convention. Results are copied
to host-owned storage, and provider-owned temporary storage is released using
the provider's own cleanup contract. An arbitrary provider allocation must
never be passed to `quantapdf_free()`.

The request record is the sole authority for admission, cancellation, and
publication. Its execution sequence is:

```text
capture -> own input -> admit -> invoke -> validate -> publish
```

Any failed stage cleans up its acquired resources and reports failure without
publishing a partial result. Cancellation before invocation releases queued
input. Cancellation during invocation marks publication ineligible but keeps
input and provider resources alive until execution ends. Late completions from
a cancelled request or retired source generation are discarded with normal
cleanup. Exactly one terminal outcome is reported per request.

Cancellation, source-generation retirement, and publication share the host's
request-state synchronization. Publication atomically checks the request token,
current source generation, and cancellation state before committing the
validated result. Cancellation that wins this commit gate prevents publication;
after successful publication, cancellation reports that the request has already
completed. A cancelled or timed-out terminal outcome remains distinct from
execution quiescence and cannot release resources still in use.

SDK errors retain their original `quantapdf_status`. Provider incompatibility,
admission rejection, provider failure, cancellation, and timeout belong to the
host error domain; this design appends no SDK status values. A timeout does not
prove provider execution has stopped, and it cannot authorize early destruction.

The host owns the derived-result store; indexes are projections of published
results and immutable source observations. Index namespace identity includes
source/snapshot identity, input policy/version, operation parameters, and
provider/model revision. Store/index commit must be atomic at the host boundary
or fail without making a partially updated index visible. Retry and durable
transaction behavior require the selected store's contract.

## Optional CMeta and Salts Plugin integration

Check a concrete integration against the selected SDK's
[Plugin contract](https://github.com/qigao/salts/blob/master/plugin/README.md)
and [CMeta inspection contract](https://github.com/qigao/salts/blob/master/cmeta/INSPECTION.md).
These upstream references do not select or pin a Salts dependency for QuantaPDF.

CMeta may describe host observation/result records when a concrete schema is
introduced. It cannot expose opaque SDK internals, grant mutation authority,
or retain snapshots. Read-only descriptor projection preserves native owner
authority; copying descriptors does not retain their provider. Type comparison
must be semantic across translation units and modules.

A future dynamic-provider adapter reuses Salts Plugin publication, contract
admission, registry, and lease ownership. The host links `Salts::Plugin`, and
the provider publication target uses `Salts::PluginABI`; these dependencies
remain outside the QuantaPDF kernel. The concrete provider interface and its
exact callable signatures need a separate implemented and tested contract.

Admission checks the installed Plugin ABI and CMeta Reflection epoch before
reading cross-module metadata, independently of the observation/result format
version and provider's package version. A live lease covers descriptor access,
binding, invocation, result copying, and provider cleanup. The registry remains
the authoritative module-lifetime owner.

Shutdown closes host request admission, cancels queued work, requests provider
stop, and waits for active execution and cleanup. Provider-dependent objects
are destroyed before releasing their leases. Unload occurs only after the
registry confirms the required quiescent state and absence of active leases.
Cancellation or timeout cannot force unload. If a provider cannot stop, the
host reports the incomplete shutdown and preserves the resources it still uses.

In-process providers execute with host process authority. Plugin loading and
contract validation do not provide a sandbox, crash isolation, or a data-egress
policy. Remote dispatch requires an explicit host destination and disclosure
policy; it never occurs as a hidden fallback. Process isolation and transport
are separate host integrations.

## Dependencies, alternatives, and migration

| Issue | Relationship to this boundary |
| --- | --- |
| [#148](https://github.com/qigao/QuantaPDF/issues/148) | Future shared snapshot model; existing text snapshots suffice for the initial host capture path |
| [#155](https://github.com/qigao/QuantaPDF/issues/155), [#162](https://github.com/qigao/QuantaPDF/issues/162) | Future canonical metadata/schema; adapters must reuse it once available rather than create a competing PDF schema |
| [#159](https://github.com/qigao/QuantaPDF/issues/159), [#163](https://github.com/qigao/QuantaPDF/issues/163) | Future graph query/index integration; current text search does not implement these models |
| [#158](https://github.com/qigao/QuantaPDF/issues/158), [#172](https://github.com/qigao/QuantaPDF/issues/172) | Backend provider and capability integration; independent of a host consuming the existing public SDK |

Embedding AI/runtime dependencies into the kernel would expand SDK dependency,
deployment, and failure contracts. Passing native handles to providers would
couple their lifetime and concurrency to the SDK. Both are rejected in favor
of copied host observations. The copy costs memory and time; no performance
benefit is claimed without measurement.

The first implementation can be a host-owned text adapter against the current
SDK. Canonical schema, durable identity, graph queries, dynamic plugins, and
image profiles are later integrations with their own compatibility tests. This
design defines semantic requirements rather than a temporary wire format or
public placeholder API.

Existing callers require no migration. Disabling a future adapter removes
intelligence work while retaining ordinary SDK operation. Provider/model
changes use separate result/index namespaces; rebuilding or deleting stored
user data requires an explicit migration decision. Rolling back this document
and its README link changes no binary, native behavior, or stored data.

## Acceptance and future implementation validation

The design is complete when the issue's extension placement, source references,
ownership, concurrency, errors, provider lifetime, and future integration
points are explicit and consistent with the existing ABI. No runtime behavior
test is added for this documentation change.

An implementation must demonstrate these cases before advertising support:

| Area | Required evidence |
| --- | --- |
| SDK compatibility | Public header/export baseline unchanged; ordinary SDK operation without a provider dependency |
| Capture lifetime | Document closed after extraction; copied input valid after snapshot drop; cleanup on accessor/copy failure |
| Provenance | Multiple pages/spans, empty pages, UTF-8 range boundaries, repeated requests on one snapshot, edited source revision, and stale-result rejection |
| Threading | All SDK calls serialized across documents and cleanup; provider workers use only independent host data |
| Capacity | Exact limit, limit plus one, checked arithmetic overflow, full queue, aggregate budget exhaustion, allocation failure, and reservation rollback |
| Results | Unsupported versions/operations, invalid references, non-finite numeric data, oversized results, and incompatible embedding namespaces |
| Publication | Provider/validation/store failure leaves no partial visible result; cancellation races with commit and late completion yield one terminal outcome |
| Shutdown | Queued cancellation, running timeout, cleanup before release, lease-gated unload, and non-cooperative provider behavior |

Existing structured-text and search tests establish the current source
contracts. They are not evidence that a host adapter, model, or plugin provider
has been implemented or validated.
