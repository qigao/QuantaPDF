# QuantaPDF development rules

The current product plan is `docs/native-feature-plan.md`. It supersedes the
older issue stages and architecture specifications.

## Fail fast or let it fail

- Use direct operations and propagate the first real failure. Stop the current
  operation or script. Do not swallow errors, continue with partial success,
  repair state, automatically retry a failed operation, retry another provider,
  or search for an alternative toolchain.
- Do not add defensive programming: repeated argument/state/resource guards,
  permissive wrappers, catch-and-continue blocks, compatibility branches,
  speculative fallback paths, or environment/tool probes.
- Decode PDF syntax and resources in their responsible parser. Report an actual
  parsing/unsupported-operation error there; do not repeat the validation through
  every layer. Internal code uses its documented inputs and invariants.
- Resource destruction is ordinary ownership. Release operation-owned resources
  on exit; do not build recovery/rollback hierarchies around failed operations.
- At a C interface, translate a backend failure once and preserve its cause.
  Do not unwind C++ exceptions through C or replace all errors with UNSUPPORTED.
- Scripts must propagate native command failures, not just scripting-language
  exceptions. Use fail-fast shell/native-command settings and direct commands.

## No compatibility obligation

- No public or private API/ABI is frozen. Functions, records, enum values,
  ownership contracts, namespaces and package targets can change or be removed.
- Change the implementation, current header, repository callers and tests
  together. Do not retain aliases, old providers, wrappers, versioned record
  negotiation, deprecated factories or migration modes for old callers.
- Export checks describe the current intended header/library pair. Update their
  manifest with API changes; an old ABI baseline must not block a redesign.
- Version numbers identify a build. They are not a promise of compatibility.

## Build the PDF features

- qpdf owns PDF file structure/read/write; the native C11 renderer owns pixels.
  Keep one document authority and one native paint pipeline.
- Replace exact paint-order/profile admission with a general ordered scene.
  Do not add another M2b.2x route for each combination of Text/Fill/Image/Clip.
- Existing small operation, glyph, edge, sample and DPI allowlists are current
  limitations to remove, not permanent product contracts. Implement appropriate
  data structures/algorithms; do not merely delete a check before a fixed array.
- Do not introduce a provider/plugin/schema/event/cache/workflow framework as a
  prerequisite for an ordinary PDF feature.

## Verification and delivery

- Test observable PDF behavior and the actual current API. Existing test counts
  and historical private refusals are not frozen. Update a refusal when the
  corresponding real feature becomes supported; do not hide missing features.
- Use the existing in-repository tests. No installer/consumer test projects,
  compiler smoke/probe projects, PDFium fallback or cross-renderer gates.
- LLVM is the toolchain. Windows uses LLVM-MinGW; Linux/macOS use runner Clang.
  Keep ccache/dependency caches, pinned inputs and curl-only external downloads.
  Do not add Mono, NuGet download helpers or compiler-version probes.
- Preserve old measured results as history. Closing an obsolete issue is not a
  statement that its missing functionality has been implemented.
