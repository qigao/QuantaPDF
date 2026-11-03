# Native LLVM CI

The Native CI workflow builds and runs the in-tree native CTests on Linux
Release, Linux ASan/UBSan, macOS ARM64 and Windows x64. Linux/macOS use the
runner's Clang; Windows and all its dependency packages use LLVM-MinGW UCRT.
The Windows shared library retains the exact `abi/quantapdf-v2.exports`
contract, checked with `llvm-readobj --coff-exports`.

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
