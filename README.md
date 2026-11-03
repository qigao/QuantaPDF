# QuantaPDF

QuantaPDF is a native PDF library with a C11 interface, qpdf for PDF file
structure/read/write, and a native C11 renderer.

## Current direction

**Fail fast or let it fail. No defensive programming. No API/ABI compatibility
obligation.** Errors end the current operation or script and retain their cause.
APIs, records, enum values, ownership rules and package targets may be changed
or removed directly. Update the implementation and its callers together.

The current design and missing-feature inventory is
[the native feature plan](docs/native-feature-plan.md). Repository development
rules are in [AGENTS.md](AGENTS.md). Older issue stages and architecture specs
are historical; they do not require compatibility layers or new frameworks.

## What exists, and what remains

| Area | Current state | Next work |
| --- | --- | --- |
| PDF structure and document operations | qpdf-backed open/read/edit/export/merge and observation APIs exist | Exercise the current API through the full relevant SDK regression set |
| Native rendering | Real Path/Image/Text/Clip profiles, curves/Stroke examples, owned results and independent pixel tests exist | General ordered page content, complete transforms and page geometry |
| Fonts and text | TrueType/CID FontFile2, subsetting, outlines and selected text operations exist | General font/text-state combinations, wider fonts, CMaps and extraction geometry |
| Images and color | Qualified RAW/Flate Gray/RGB source profiles exist | JPEG/scanned images, wider color spaces, masks and complete affine placement |
| Composite graphics | Writer/Composer pieces exist | Native Form, transparency/group, shading/pattern and appearance rendering |
| Delivery | Native platform CI is green on the recorded baseline | Current feature/API documentation, ordinary-file coverage and release packages |

The current Source frontend still has small fixed profile restrictions.
Those are implementation limitations to remove, not the desired product scope.
Writing a PDF construct does not imply that native page rendering implements it.
Unsupported functionality reports its actual error; there is no PDFium fallback.

The plan records exact merged/candidate identities. In particular, an unmerged
PR is not part of the current library even when its own CI passes.

## Build and test

Use the pinned dependency/compiler environment from
[the workflow](.github/workflows/ci.yml) and [CI notes](docs/ci.md).
Windows uses LLVM-MinGW; Linux/macOS use runner Clang. Both compiler and
package/download caches reuse the same build inputs. External downloads use curl.

With that environment configured:

```sh
cmake --preset ci-native
cmake --build --preset ci-native
ctest --preset ci-native --stop-on-failure
cmake --install build/ci-native --prefix stage/sdk
```

The sanitizer preset is `ci-asan-ubsan`: Debug with `-O1`, debug symbols,
ASan/UBSan, nonrecovering UB and frame pointers. Native command failure ends
execution; there are no compiler-version probes or installer/consumer test projects.

At the 2026-11-03 baseline, top-level CMake registers `tests/native`.
Its 69 tests are evidence for that commit, not proof that every old SDK test
or every PDF feature has run. See [product verification](docs/product-conformance.md).

## SDK

The current header is [`quantapdf/quantapdf.h`](include/quantapdf/quantapdf.h).
The current CMake entry is:

```cmake
find_package(QuantaPDF CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE QuantaPDF::QuantaPDF)
```

Use the header and library from the same build. Version numbers identify builds;
they do not select a compatible older API. Existing versioned factories and
record-size negotiation are cleanup work in A1, not a promise to keep them.

The [current SDK notes](docs/native-sdk.md) cover the interface and package.
The [runtime cutover record](docs/native-runtime-cutover.md) preserves measured
implementation history. The former detailed API examples remain available in
[the baseline README](https://github.com/qigao/QuantaPDF/blob/c68b89346aa2e3f01cd4c17daeee6aea7cdbd3b0/README.md)
as a source snapshot, not a compatibility contract.

## Beta scope

A developer Beta should provide a useful current API, ordinary multi-paint
pages, common embedded text/images, basic document output and creation,
corresponding real-file tests and usable packages. The feature plan lists the
remaining work and the explicitly deferred features. Completing every advanced
PDF or host-integration feature is not a prerequisite for that Beta.

## License

See [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md). qpdf, ZLIB and JPEG
remain private build dependencies. There is no PDFium build/runtime or package.
