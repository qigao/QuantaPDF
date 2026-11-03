# QuantaPDF v2 API Stability Design

> Superseded on 2026-11-03. No API/ABI compatibility is required. Use the
> [current feature plan](../../native-feature-plan.md) and
> [development rules](../../../AGENTS.md). This file preserves the old design
> as history; its compatibility policy is not an implementation requirement.

## Goal

Turn the current v2 C11 interface into a release-candidate ABI by making its
version explicit, constraining the shared-library export surface, and stating
the threading contract without expanding the PDF feature set.

## Public ABI boundary

`include/quantapdf/quantapdf.h` is the sole public API source. The v2 export
baseline contains exactly the functions declared with `QUANTAPDF_API` in that
header. Test hooks, backend bridges, and implementation helpers are never
shared-library exports.

The Windows Release test build is the first enforced binary boundary because
it produces the project's current shared-library artifact. Its actual PE
export table must exactly match `abi/quantapdf-v2.exports`. A missing public
symbol or any unexpected symbol fails CTest.

The fault-injection entry points remain test utilities. They are compiled into
the test executables that call them, while the library retains its private
fault-state fields under `QUANTAPDF_TESTING`. Their declarations use ordinary C
linkage rather than `QUANTAPDF_API` import/export decoration.

## Version contract

The public header defines:

```c
#define QUANTAPDF_VERSION_MAJOR 2
#define QUANTAPDF_VERSION_MINOR 0
#define QUANTAPDF_VERSION_PATCH 0
#define QUANTAPDF_ABI_VERSION 2
```

These values match `project(QuantaPDF VERSION 2.0.0)`. Shared-library targets
use the project version and major-version `SOVERSION` where the platform
supports versioned shared-library filenames.

## Compatibility policy

Within ABI version 2:

- existing exported functions, enum numeric values, ownership rules, and
  structure fields are not removed or reordered;
- new functions and enum values may be appended;
- append-only single structures use `struct_size` negotiation;
- fixed-stride V1 array element structures are replaced by new types/APIs
  rather than enlarged;
- a deliberate incompatible change increments `QUANTAPDF_ABI_VERSION` and the
  shared-library major version.

## Threading contract

The v2 API is externally serialized. Applications must not execute QuantaPDF
API calls concurrently, including calls operating on different documents.
This restriction is part of v2 rather than a temporary migration note. A later
concurrent API requires an explicit contract change backed by concurrency
tests.

## Verification

- A compile/runtime test verifies the public version macros.
- A Windows CTest reads the real DLL export table with `dumpbin` and compares
  it to the committed v2 baseline.
- The existing 25 behavior test groups remain green.
- Windows Release installation succeeds through the install preset.
- Linux Release plus sanitizer, macOS, and Windows CI pass on one head SHA.

## Non-goals

- Adding new PDF transforms.
- Claiming thread safety.
- Publishing a release or tag.
- Introducing a runtime backend selector.
