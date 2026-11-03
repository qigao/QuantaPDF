# Product verification

The [current feature plan](native-feature-plan.md) replaces the former staged
product gate. Verification follows actual PDF functions and the current API.
There is no compatibility requirement for old declarations or old test layouts.

## What runs today

The top-level `CMakeLists.txt` registers `tests/native` when tests are enabled.
At master `c68b89346aa2e3f01cd4c17daeee6aea7cdbd3b0` this is 69 tests.
[Native CI #2407](https://github.com/qigao/QuantaPDF/actions/runs/41626185987)
passes them on Linux Release, Linux ASan/UBSan, macOS ARM64 and Windows
LLVM-MinGW. The Windows job checks the actual DLL exports.

```sh
cmake --build --preset ci-native
ctest --preset ci-native --stop-on-failure
cmake --build --preset ci-asan-ubsan
ctest --preset ci-asan-ubsan --stop-on-failure
```

Windows/macOS jobs are ordinary matrix jobs; there is no `full-ci` label gate
in the current workflow. The current CI does not run an installer/consumer
project, a Poppler/PDFium comparison, or all the old SDK test registrations.
It also does not upload SDK release artifacts.

## Functional coverage to finish

| Function | Existing evidence | Work needed |
| --- | --- | --- |
| Native painting/geometry | Source originals, pixel references, Clip/Stroke and device tests | General ordered pages and full geometry (A2) |
| Text/fonts/images | Selected TrueType/CID and RAW/Flate tests | General combinations and wider formats (A3/A4) |
| Form/alpha/pattern/appearance | Some writer/Composer code and historical tests | Actual native output (A5) |
| Document/edit/export/compose/security APIs | Implementations and legacy tests exist; one annotation transaction test is in the native suite | Port relevant tests to current APIs and run the functions promised by the release (A6) |
| Ownership/failure | Real Context/Source/C11 failure and lifetime tests | Keep useful correctness coverage without compatibility/defensive guard targets (A1/A7) |
| SDK artifacts | Install rules exist | Build identities, package contents, notices, checksums and actual promised platform artifacts (A7) |

Test counts can change. Rewrite old callers when the API changes. A private
primitive passing, an intentionally unsupported case, or code merely existing
is not evidence that a public feature works.

## Verification rules

- Test full observable behavior: original input, requested operation, resulting
  pixels/objects/bytes and a usable output. Use independently authored expected
  values or mathematical references for pixel/geometry claims.
- Stop at a real failure and retain its cause. Do not skip an operation, select
  another backend or turn an unimplemented feature into a passing empty result.
- Test normal multi-paint pages, text/font combinations, scanned images,
  composite graphics and reopened edited/generated documents as they are added.
- Move valuable existing SDK tests into the existing in-tree organization; do
  not create installer/consumer projects, compatibility suites or probe builds.
- Retain exact commit/run identities for reported results. Historical failed
  runs remain historical failures; a candidate run is not a merged-head run.
- Update the current export manifest with the header. It verifies intended
  exports, not an obligation to preserve an old ABI.

Old measured fixtures and reports remain useful history, not a requirement to
retain a bug, a profile-specific refusal, a fixed number of tests or a tiny
experimental resource ceiling. Beta coverage is the declared implemented
feature set and its actual artifacts, not completion of unrelated frameworks.
