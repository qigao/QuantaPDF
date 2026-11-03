# Current native SDK

The [feature plan](native-feature-plan.md) and [repository rules](../AGENTS.md)
supersede the old API stability policy. **No public/private API or ABI must
remain compatible.** Changes update the implementation, current header,
repository callers and tests together. Do not introduce old-name aliases,
versioned record adapters, deprecated factories or migration providers.

## Current build boundary

`include/quantapdf/quantapdf.h` declares the current C11 interface. qpdf objects,
C++ implementation types and native raster implementation records remain
internal because callers do not need them for the PDF operations.

C calls use the current declarations, native record layout and matching
platform libraries. `size_t` maps to the caller's native size type; C `int`,
`uint32_t`, `float` and `double` keep their declared representation. Text and
paths use UTF-8. These are current calling requirements, not cross-version
negotiation or guarantees about future layouts.

Records have one current layout. Initialize input records with zero and set the
fields the operation uses; output records are written directly. There is no
`struct_size`, record-version negotiation or ABI-version macro. Build callers
against the matching current header and library.

The three root factories take an explicit Context:

```c
quantapdf_context *context;
quantapdf_document *document;
quantapdf_status status = quantapdf_context_create(&context);
if (status != QUANTAPDF_OK)
    return status;
status = quantapdf_open(context, "document.pdf", NULL, &document);
quantapdf_drop_context(context);
if (status != QUANTAPDF_OK)
    return status;
/* Use document; it owns its Context reference. */
quantapdf_close(document);
```

`quantapdf_composer_create(context, options, out_composer)` accepts NULL options
for defaults. `quantapdf_merge_outputs(context, inputs, count, out_output)` uses
the chosen destination Context; its input Outputs may belong to other Contexts.
Each successful factory retains the Context. There are no implicit-root
factories or `_with_context` aliases. Mandatory handles, pointers and output
slots must be valid. Font registration and SVG drawing have no empty options
argument.

## One ownership and error model

Objects have their declared owner and one destruction path. Context references,
Document/Page relationships and owned results are ordinary lifetime rules.
Borrowed views are used while their owner lives. Returned owned bytes/handles
are released through their current release operation.

A1 must make the public declarations, tests and documentation agree on these
lifetimes, including the native Bitmap result that tests keep after source
retirement. Do not add a wrapper to preserve an old ownership rule. The current
implementation has not acquired a concurrency guarantee from this design;
independent/same-object concurrency must be implemented and tested if exposed.

An actual parse, decode, I/O, allocation, write or unimplemented-operation
failure ends that operation. Keep the cause and translate it once at a C entry.
Do not unwind a C++ exception through a C caller. Do not catch an error to
continue, retry another provider, repair the input or return partial success.
Temporary resources are destroyed by their owner on exit.

Internal routines use their documented inputs and invariants. PDF grammar and
resource decoding belong to their responsible parser, rather than repeated
argument/state/resource guard chains throughout the runtime. No defensive
compatibility or recovery framework is required.

## Build and package

The authoritative compiler/dependency setup is
[the current workflow](../.github/workflows/ci.yml) with
[presets/CI.json](../presets/CI.json). Windows uses LLVM-MinGW; Linux/macOS use
runner Clang. Use the selected platform/triplet and the same build's libraries.

With that environment configured:

```sh
cmake --preset ci-native
cmake --build --preset ci-native
ctest --preset ci-native --stop-on-failure
cmake --install build/ci-native --prefix stage/sdk
```

There are no installer/consumer test projects or compiler probes. The existing
native setup uses ccache and pinned dependency/download caches with curl-only
external transfers. A native command failure must terminate its script.

The install layout currently contains:

| Location | Contents |
| --- | --- |
| `include/quantapdf/quantapdf.h` | Current public declarations |
| `bin/` and/or `lib/` | Native library/import library; static builds include their required private backend archive |
| `lib/cmake/QuantaPDF/` | Current package config and targets |
| `share/quantapdf/` | Project/dependency notices and SDK notes |

```cmake
find_package(QuantaPDF CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE QuantaPDF::QuantaPDF)
```

These are the current names and layout, and may change with the API. Version
numbers identify a build, not a supported older caller. Package version matching
and the historical v2 export-baseline name are cleanup items under A1. An export
check should describe the current intended header/library pair and be updated
with API changes.

## Scope and evidence

Native rendering currently implements selected Source/IR/raster profiles.
General content, font, image, composite-graphics and document-operation gaps are
listed in [A1–A7](native-feature-plan.md). Old private function versions are not
separate products or compatibility levels to preserve.

Current platform CI runs the in-tree native suite. It does not automatically
execute the legacy full SDK suite or produce released SDK packages. A7 migrates
relevant tests to the current API and verifies the actual artifacts promised by
the release. Official non-C bindings and parallel worker/service frameworks
remain later work, not prerequisites for ordinary PDF operations.
