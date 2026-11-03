# Native LLVM CI

The Native CI workflow builds and runs the in-tree native CTests on Linux
Release, Linux ASan/UBSan, macOS ARM64 and Windows x64. Linux/macOS use the
runner's Clang; Windows and all its dependency packages use LLVM-MinGW UCRT.
The Windows job currently compares exports with `abi/quantapdf-v2.exports`
using `llvm-readobj --coff-exports`. Under the [current plan](native-feature-plan.md),
that historical filename is not an API/ABI compatibility promise. Update the
intended export manifest with the current header when an API changes.

`ci-native` and `ci-asan-ubsan` are separate from developer presets. The shared
vcpkg toolchain chainloads the selected compiler, and both C and C++ project
compilation use ccache. No Clang version checks or compiler smoke projects run.

Cache layers follow SaltsCore's design:

- vcpkg binary packages and source downloads are cached by OS, architecture,
  exact triplet, shared overlay revision, runner image, Windows compiler release
  and product manifest. vcpkg still validates package ABIs on restoration.
- ccache uses compiler-content identity, a 512 MiB limit and independent entries
  per platform/preset. Each source revision saves a new entry and restores the
  most recent compatible entry. Cache statistics are reported after building.
- Dependencies are saved after configure; compiler results are saved after a
  successful build, even if a later runtime test fails. CMake build trees are
  never restored as caches.

The shared infrastructure is fixed at the curl-only revision that removed
Mono/NuGet and replaced compiler downloads with curl. Native ccache archives
also use curl and pinned SHA-256 values. Product cache misses build pinned
sources into the local vcpkg files cache; they do not require every dependency
ABI to have been published to GitHub Packages. No installer/consumer projects
are built by CI. Runtime CTests, sanitizers and the real Windows export check
remain the acceptance gates.

## Failure behavior and development direction

Fail fast or let it fail. Native command failures terminate their script;
PowerShell scripts must propagate native exit failures as well as cmdlet
exceptions. No catch-and-continue, alternate toolchain/backend, environment
probe or compatibility path is part of CI. The caches accelerate the same
pinned build rather than select a different implementation after an error.

Debug/O1 ASan/UBSan, the existing runtime tests and current export verification
remain real checks. Their test count and historical ABI contents are not frozen.
A1 cleans up old script/API guard layers; A7 brings relevant SDK behavior tests
and actual release artifacts into this same in-tree workflow. See
[the feature plan](native-feature-plan.md) and
[product verification](product-conformance.md).
