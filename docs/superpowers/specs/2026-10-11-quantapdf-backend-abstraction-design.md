# Internal PDF backend abstraction

Tracks [#143](https://github.com/qigao/QuantaPDF/issues/143).

## Problem and scope

The C document, page, extraction, transform, and edit facades currently call
PDFium/qpdf bridges directly and store their concrete handles. The public
header already hides those types, but the runtime cannot dispatch those
operations through another internal implementation.

Introduce a private C operation table and opaque provider state. The built-in
provider composes PDFium reading/rendering/extraction with qpdf structural
inspection/transforms/editing. The two engines remain complementary; neither
is a fallback provider for the other.

This change does not introduce provider selection, dynamic loading, a public
provider ABI, concurrency, or another PDF engine. Composer/font/SVG lowering
and private image codecs retain their existing bridges; their separate
architecture work belongs to
[#151](https://github.com/qigao/QuantaPDF/issues/151) and
[#154](https://github.com/qigao/QuantaPDF/issues/154).

## Placement and dispatch

```text
Public quantapdf_* C11 facade
  |
  +-- document/page -- quantapdf_backend_ops + opaque document/page state
  |                         |
  |                         +-- built-in composite
  |                                +-- PDFium reader
  |                                +-- qpdf structural engine
  |
  +-- edit -- quantapdf_backend_edit_ops + independent edit state
```

[document_backend.h](../../../src/backend/document_backend.h) owns the private
operation contracts and neutral bitmap/test-observation carriers.
[builtin_backend.c](../../../src/backend/builtin_backend.c) alone translates
these document operations into the concrete bridge calls. Each adapter has an
exact C callback signature; no function-pointer cast or incompatible `T **`
to `void **` cast is used.

Documents retain an immutable ops pointer and opaque state. Pages borrow their
parent document and use that document's ops for page operations. Edits retain
their own immutable ops pointer and independent state, allowing the source
document to close before an edit snapshot or mutation.

The existing public open entry explicitly chooses the built-in table. Stateless
output merging explicitly uses the same built-in composition. There is no
mutable registry, global provider override, or implicit discovery. All table
entries are implemented; the private table is trusted internal code, and does
not accept unvalidated external/plugin tables.

## Ownership and failure protocol

- The facade owns source bytes and its copied password. Provider open borrows
  them until provider close. The nullable password passed to PDFium preserves
  the distinction between no password and an explicitly supplied empty string;
  qpdf retains its existing normalized empty-password behavior.
- Composite open allocates one fixed-size state, opens PDFium first, then qpdf.
  It publishes state only after both succeed. Failure closes all acquired
  engine handles, frees the composite, and leaves the caller's output NULL.
- A loaded page remains tied to the same document/provider. Its drop callback
  runs before document close. No new per-page wrapper allocation is introduced.
- Document close destroys both engines before the facade frees source bytes
  and wipes/frees the password. The existing engine cleanup order is retained.
- Edit begin returns an independent provider-owned graph. Its ops table must
  outlive the edit without borrowing the source document. Edit drop releases
  that graph exactly once.
- Snapshot results retain their existing copied-observation lifetimes. Render
  samples and transform bytes keep their existing allocator and cleanup
  contracts; the operation table does not change public drop/free functions.
- Public argument validation, output-reset behavior, `struct_size` negotiation,
  fault injection, and provider status propagation remain in their existing
  owners. Dispatch adds no status conversion, retries, or fallback path.

The built-in tables are process-lifetime constants. The composite state has two
engine pointers and no queue, registry, cache, or variable-capacity storage.
ABI v2 still requires external serialization across all calls and documents.
Plugin leases and provider capability admission remain separate work under
[#158](https://github.com/qigao/QuantaPDF/issues/158).

## Alternatives and compatibility

Renaming bridge functions would retain direct concrete dispatch. A global
provider registry would introduce selection and lifetime state that this issue
does not require. Independent public reader/writer handles would alter ABI and
expose the current engine composition. A per-document immutable private table
keeps dispatch explicit while preserving current callers.

The public header, v2 export baseline, dependencies, configuration, and output
formats remain unchanged. The cost is one fixed-size allocation per document,
thin adapters, and indirect calls. No performance improvement is claimed.

Migration replaces facade bridge calls while retaining the existing bridge
implementations and algorithms. Reverting the facade routing and private
adapter restores the previous internal layout without changing persisted data
or requiring callers to rebuild against another ABI.

## Verification

Extend the existing backend-foundation CTest with a replacement provider to
exercise public page/render/text/export/edit calls, failure propagation, and
edit/snapshot lifetime after source close. Exercise the built-in table against
the existing real PDF fixture and retain all direct engine foundation cases.

First run backend foundation, document, render, structured-text, text-search,
export, and edit regressions through the existing user preset, then run the
full CTest suite and binary export validation. Debug/ASan uses its separate
existing preset. These are validation requirements; actual results are reported
with the implementation rather than assumed by this document.
