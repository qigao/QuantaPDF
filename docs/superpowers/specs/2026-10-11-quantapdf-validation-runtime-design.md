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

First-failure order is deliberate and deterministic: top-level storage shapes,
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
model. A generic visitor/report framework is unnecessary for the two current
input models. Typed validators share the actual report contract and policy rules.

The generic Document Graph from #146 does not exist yet. Its object/stream graph
adapter and broader structural validation remain required follow-up for #149;
this implementation does not close that gap. Future CMeta descriptors can name
these typed subjects and rules, but cannot replace input ownership or parser
admission. Reverting this commit restores prior internal checks; no persisted
format or public ABI migration is needed.

## Verification

Use backend-free C tests for deterministic first failures, array and payload
shapes, every resource-reference family, operation and navigation references,
known classifications, forbidden/allowed security bits and report reset. Add an
IR export integration test for rejection before output publication and recovery
after repairing the model. Existing public Composer, security, rewrite and
sanitization tests verify compatibility; run focused tests then full Release and
sanitizer CTest, native SDK installs and Linux static CI. This work makes no
performance or external-standard conformance claim.
