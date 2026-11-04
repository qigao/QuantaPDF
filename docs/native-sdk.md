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

`quantapdf_render_options`, `quantapdf_composer_graphics_state_options` and
`quantapdf_composer_soft_mask_options` are current complete records: initialize
all fields; they have no `struct_size` negotiation. Graphics states consume
their Clip/SMask IDs directly; zero omits an optional reference. Other areas of
the header still contain
ABI-version macros, size fields, V1/MIN_SIZE constants and duplicate factories;
their cleanup remains A1 work, without compatibility obligations.

## One ownership and error model

Objects have their declared owner and one destruction path. Context references,
Document/Page relationships and owned results are ordinary lifetime rules.
Borrowed views are used while their owner lives. Returned owned bytes/handles
are released through their current release operation.

A loaded Page retains its Document. `quantapdf_close` releases the caller's
Document reference; loaded Pages still render after that call and after the
caller drops its Context reference. The final Page releases qpdf, source bytes
and password; a completed Bitmap independently retains its Context and pixels.
The native renderer borrows the already opened/decrypted qpdf document for
Source construction and publishes owned IR, including decoded image/font data,
before the C11 raster runs. It does not reopen or reserialize original bytes.
Password-bearing Documents use the same renderer. qpdf directly parses the
selected Page's original content streams, including filter arrays/chains and
mixed encodings across a Contents array. Source no longer creates an empty
qpdf document or a second decoded-content copy. Tested content filters are
RAW, Flate, ASCIIHex, ASCII85 and RunLength; Image XObject codec coverage is
unchanged. The only Source entry, `quantapdf_qpdf_page_to_ir`, takes the opened
Document, selected page index and current page-work limits. The old byte-based
versioned entries, fixture selector and capability-policy matrix are removed;
current Source code lives in `qpdf_page_ir.h/.cpp`. Page-work limits have no
whole-file byte cap. Public rendering has no four-stream/64-KiB Contents admission cap.
Explicit internal content budgets count selected streams before parsing and
qpdf's actual decoded content bytes before interpretation; they do not bound
qpdf's transient decode buffer allocation. Two Image paints may retain a frozen
Clip for the first and GS0 after an actual Q restore for the second; the same
C11 ordered compositor consumes both. Tests include clear/encrypted files larger
than 3 MiB and independent full-RGB restored-image references. Fixed paint,
geometry, font and DPI limits remain A2 work. Fill/Image q/Q state uses one
dynamic frame stack for CTM, colors, saved Clip identity and currently interpreted
stroke metadata. That interpreter no longer applies a 16-frame or 512-token gate;
actual content/paint budgets still apply. The current path belongs to the content
stream and survives q/Q; its segments retain their own construction CTM. Tests cover 4097 nested frames, long
balanced state traffic, selected/encrypted/split-stream pages and real full RGB.
Text-internal state and the general ordered scene remain A2/A3 work. Failed open/render calls
publish no handle/Bitmap and preserve the failure status. Do not add a wrapper to preserve an old ownership rule. The current
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
