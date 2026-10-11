# Validation runtime

Implementation decision for [#149](https://github.com/qigao/QuantaPDF/issues/149),
on the immutable-snapshot baseline `a1ff84e`.

## Evidence and objective

MED, fact: Composer validates public inputs as they are accepted, but the owned
IR has no common consistency check. `qpdf_composer.cpp` checks some resource
references while emitting objects, and consumes others directly. Those checks
do not provide a backend-independent diagnostic for another IR consumer.

MED, fact: `qpdf_document.cpp` combines PDF traversal with action/subtype
classification, signature/encryption admission and sanitizer postconditions.
The classifications and policy decisions need no QPDF types. Traversal budgets,
PDF object admission, alias analysis, signature discovery and parser failures
do require the current backend and remain its responsibility.

Introduce a private pure-C validation module consuming the owned IR and copied
security facts. Integrate it in the existing export and security paths without
changing the public C11 ABI or supported policies. Preserve public argument
validation, parser statuses, codec admission, and transactional sanitization.

## Reports and ownership

Validation borrows a live model for one synchronous, externally serialized call.
It does not allocate, retain, mutate, log, repair, or invoke provider callbacks.
The caller owns the fixed-size report, which contains the first failed rule,
subject family, zero-based record index, and related reference or finding bits.
Reports contain values only, never pointers, parser messages or process IDs.
Success and argument failure reset the report; callers must check the status.

IR first-failure order is deliberate and deterministic: top-level storage shapes,
pages, resources in declared family order, operations, then navigation. Within a
record, rule order is source order. One report is the complete fail-fast result,
not an implicitly truncated list. A later multi-finding API must have an explicit
capacity and continuation contract rather than adding hidden allocation here.

IR storage and NUL-terminated strings must already be owned and addressable as
declared. Validation checks array-size arithmetic, required payloads, finite
geometry, resource kinds, referential integrity and navigation ancestry; it
cannot prove that an arbitrary pointer addresses an allocation. It is not a PDF
parser or a boundary for untrusted process memory. Font/codec/PDF payload parsing
remains with the established providers. Invalid internal model invariants return
BACKEND; invalid validator arguments return ARGUMENT. Valid Composer output and
its bytes must remain unchanged.

## Security and compliance

The backend validates PDF object types and supplies decoded PDF names with
explicit lengths. Pure-C classification reproduces the current action and
rich-media categories. A policy check receives observed audit flags and an
explicit forbidden mask. Known observations with forbidden bits return
UNSUPPORTED and a deterministic report; unknown observation bits fail BACKEND
rather than being treated as safe. Invalid policy masks fail ARGUMENT.

Existing call sites provide their existing masks: signature/encryption before
sanitization or image recompression, signatures before security rewriting, and
the selected sanitizer bits after mutation. Audit has no forbidden bits and
continues reporting observations rather than rejecting signed/encrypted input.
The runtime never turns parse failure into a successful compliance result.

Compliance here means satisfying the selected existing security policy. PDF/A,
PDF/UA and other external standards have no implemented profiles and must not be
claimed as passed. Their rules and evidence requirements need separate work.

## Dependencies, alternatives and rollout

Keep the module under `src/validation`, independent of Composer state, contexts,
QPDF, PDFium and future CMeta. Compile its sources into the existing private
archive so shared/static SDK closure stays unchanged; archive grouping is not a
source dependency on a provider. Tests compile the same C implementation alone,
without linking any backend, proving that rules and reports stand independently.

Invoke IR validation at the writer admission boundary, which is shared by main,
form and tiling-pattern export and direct IR callers. Leave existing defensive
writer checks in place. Move security classifications and mask decisions to the
new runtime while leaving graph discovery and mutation with qpdf. No alternate
parser, recursive generic object graph, schema system or public diagnostic API
is introduced.

Keeping only backend checks would not serve other IR consumers. Duplicating
the PDF parser in C would lose backend semantics and require a second ownership
model. These typed inputs do not require a generic visitor/report framework.
Typed validators share the actual report contract and policy rules.

The initial foundation above covered IR and security facts. The subsequent
[Document Graph capture](2026-10-11-quantapdf-document-graph-identity-design.md#imported-graph-implementation-protocol)
adds a copied object/stream model and a separate pure-C validator. Its first-
failure order is source-fact shape admission, storage admission, nodes in local-ID order with their edges,
partition completion, trailer/catalog roots, then ordered page entries. It
checks scalar shapes, sorted dictionary keys and references, while allowing
shared/cyclic graph references. Page-tree extraction admission remains with
the adapter. Immutable commits additionally call the pure-C page validator after
structural admission: ordered tree leaves must exactly match the index, and
tree nodes must be indirect typed dictionaries with no repetition or cycles.
The caller supplies bounded scratch; depth exhaustion returns UNSUPPORTED.
Parent/count consistency and broader PDF structural/compliance rules and feature migration
remain required follow-up for #149/#146. Future CMeta descriptors can name these
typed subjects and rules, but cannot replace input ownership or parser admission.
No persisted format or public ABI migration is needed.

## Verification

Use backend-free C tests for deterministic first failures, array and payload
shapes, every resource-reference family, operation and navigation references,
known classifications, forbidden/allowed security bits and report reset. Add an
IR export integration test for rejection before output publication and recovery
after repairing the model. Existing public Composer, security, rewrite and
sanitization tests verify compatibility; run focused tests then full Release and
sanitizer CTest, native SDK installs and Linux static CI. This work makes no
performance or external-standard conformance claim.

## Runtime regression model

For [#152](https://github.com/qigao/QuantaPDF/issues/152), the regression model is
the set of observable contracts below, exercised by the existing CTest suites.
It adds no test runner, provider registry, public report format, or serialized
test-only document representation.

| Contract | Existing executable evidence | Comparison boundary |
| --- | --- | --- |
| Native ABI admission | `quantapdf.version`, `quantapdf.abi_exports`, `quantapdf.abi_checker_rejects_extra_export`, public option/result tests | Published version, actual shared exports, accepted structure prefixes, status and output reset; static builds omit the shared-export checks. |
| Context and result lifetime | `quantapdf.context`, `quantapdf.ir`, `quantapdf.graph_snapshot` | Results survive their documented producer lifetime; finalization occurs once; failed construction restores retained-owner counts. |
| Backend dispatch and failures | `quantapdf.backend_foundation`, `quantapdf.graph_snapshot`, public document/transform tests | Real facade dispatch, status propagation, absent output after failure, and complete snapshot admission. |
| Snapshot and editor isolation | `quantapdf.pdf_annotation_mutation`, `quantapdf.pdf_form_mutation`, `quantapdf.graph_snapshot` | Source bytes and prior snapshot borrows remain unchanged; independent editors reject each other's references. |
| Edit operation rollback | `quantapdf.pdf_annotation_mutation`, `quantapdf.pdf_form_mutation` | Return and exception exits restore covered mutations; retry agrees with a clean session; restoration failure blocks subsequent session observation and publication. See [transaction boundaries](2026-10-11-quantapdf-edit-transactions-design.md) for scope. |
| Immutable graph commits | `quantapdf.graph_edit`, `quantapdf.graph_snapshot` | Exact-base admission, independent branch origins, inherited identity, bounded copy/validation failure, deterministic models and detached snapshot lifetime; no claim of graph-to-PDF output or IR mapping. |
| Private graph writer | `quantapdf.graph_write`, `quantapdf.graph_snapshot` | Reopened committed page order, binary metadata and encoded streams; deterministic output, preserved versions/extensions, source encryption provenance, direct-cycle/depth/expansion/output rejection and detached output lifetime. Encrypted/signed output and IR mapping remain follow-up. |
| Resource reuse | `quantapdf.resource_registry`, `quantapdf.ir`, public Composer resource tests | Equal owned resources reuse local IDs; rejected admissions and nested rewind preserve the owning registry. |
| Deterministic logical reports | `quantapdf.validation`, `quantapdf.graph`, `quantapdf.identity` | Value-only rule/subject/index/detail, explicit first-failure precedence, reset on success, independent model storage, and checked ID exhaustion. |
| Deterministic output and capture | `quantapdf.composer`, `quantapdf.ir`, edit mutation tests, `quantapdf.graph_snapshot` | Equivalent operations in the same qualified build produce equal bytes where promised; repeated capture produces equal logical node/edge/value data. |

Tests of pure rules compile the production C validator/identity/graph sources
without a provider library. Public behavior assertions use SDK-visible values.
Private boundary tests may inspect owned models and lifecycle counters; adapter
tests such as `quantapdf.graph_import` separately qualify QPDF extraction. A
provider fixture helper is not the oracle for an SDK ownership or status rule.
The test boundary remains usable when a provider is replaced.

Determinism is scoped to a declared operation, fixture, options and qualified
build. Compare model fields rather than C struct padding, allocation addresses,
runtime scope numbers, PDF object numbering across providers, or log timestamps.
Reports for equal logical defects must agree even when all model arrays occupy
different storage; repairing one model must not change another model's report.
Faults are removed in priority order to verify the next error and eventual reset.
Encoded payload decoding remains a provider responsibility, not a pure-validator
test expectation.

Encryption intentionally uses fresh randomness. Its tests compare decoded
content, policy, password/permission behavior and failure atomicity, rather than
requiring equal ciphertext or weakening the random source. Pixel/font output
across provider upgrades needs explicit qualification; same-build byte equality
does not certify cross-version rendering equivalence.

Run the affected registered CTests first, then the native or sanitizer suite for
the changed runtime/profile. Configure determines CI's complete build graph;
build that graph and run tests through CTest. Linux development flags disable
UBSan recovery for both C and C++ so a diagnostic cannot leave a successful exit
status; see the [GCC recovery contract](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html#index-fsanitize-recover_003d).
Windows instrumentation and its prebuilt-dependency limitations are recorded in
the [graph acceptance protocol](2026-10-11-quantapdf-document-graph-identity-design.md#acceptance-evidence).
Report actual commit, profile, passed/failed counts, timeouts, platform omissions,
and known qualification warnings. A timeout or incomplete run is not a pass.

This is regression evidence for the implemented runtime, not exhaustive ABI
record-layout coverage or proof of absent defects. Future graph transactions,
arbitrary-PDF IR lowering, bindings and concurrency policies require new cases
when implemented. The current ABI still requires process-wide external
serialization; the suite does not imply a concurrent-call contract. PDF/A/UA
certification, aggregate allocator quotas and coverage of all allocation-failure
sites remain outside these tests.
