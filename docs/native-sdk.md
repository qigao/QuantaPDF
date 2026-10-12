# Native SDK contract

This is the consumer contract for the current C11 SDK and its CMake package.
It addresses [#141](https://github.com/qigao/QuantaPDF/issues/141), with ownership
and evolution rules for [#139](https://github.com/qigao/QuantaPDF/issues/139),
[#168](https://github.com/qigao/QuantaPDF/issues/168) and
[#171](https://github.com/qigao/QuantaPDF/issues/171).
The installed `quantapdf/quantapdf.h` is the API source of truth. The repository
does not yet ship official Rust, Go or C# binding packages; the rules below
describe how those callers use the same native ABI.

## Boundary and supported builds

Public functions have C linkage. Public records contain C scalars, pointers,
enums and other public records. Object handles are incomplete types; consumers
must not allocate, copy, inspect or free their private layouts. No PDFium/qpdf
type, STL type, exception object, allocator implementation or backend header is
part of the public API. Backend exceptions are translated inside the library.

The supported artifact matrix currently follows the pinned backend builds:
Windows x64, Linux x64, and macOS x64 or arm64 as separate builds. There is no
universal macOS binary or 32-bit SDK in this matrix. A binary must match the
caller's operating system, architecture, calling convention and runtime. Static
linking also requires compatible C++ toolchain and dependency builds. A stable
C ABI does not make a static C++ implementation portable between toolchains.

Use default native record alignment and enum representation. Compiler options
that change packing, enum size or the calling convention are outside this ABI.
Do not translate `size_t` to a fixed 32-bit integer, use native `long` as a
portable replacement for a fixed-width integer, or marshal C `int` flags as a
language-specific boolean representation.

## CMake package and installation

Build and install through the existing user presets, for example from a VS
developer environment on Windows:

```powershell
cmake --preset win-release-user
cmake --build --preset win-release-user --parallel 2
ctest --preset win-release-user --output-on-failure
cmake --build --preset install-win-release-user --parallel 2
```

The selected preset owns the dependency triplet and install prefix. Keep Debug,
sanitizer and Release installations separate. The install tree contains:

| Location (GNUInstallDirs names) | Contract |
| --- | --- |
| `include/quantapdf/quantapdf.h` | Sole public header. |
| `bin/` and/or `lib/` | QuantaPDF library, Windows import library when shared, and bundled PDFium runtime. Static SDKs also include the private backend archive and, on Windows, PDFium's import library. |
| `lib/cmake/QuantaPDF/` | Relocatable package config, version config and exported targets. |
| `share/quantapdf/` | Project notices, dependency license material and this contract. |

Consumers select a matching installation, then link the exported target:

```cmake
# For a Salts-family consumer, QUANTAPDF_ROOT comes from its user preset.
find_package(QuantaPDF 2.34 CONFIG REQUIRED
  PATHS "$ENV{QUANTAPDF_ROOT}" NO_DEFAULT_PATH)
target_link_libraries(my_application PRIVATE QuantaPDF::QuantaPDF)
```

`QuantaPDF::QuantaPDF` supplies the include path, C11 requirement, shared-library
import definition when needed, configuration-specific library and link closure.
It is the only supported consumer target. Names beginning with `QuantaPDF::_`
are private package implementation details. No package components are currently
defined. Moving the complete installation preserves its internal paths; moving
individual libraries without their headers, config and runtime is not relocation.

Shared consumers do not need qpdf/PDFium development headers or qpdf CMake
packages. Static consumers resolve qpdf 12.4.0, ZLIB and JPEG through their
existing vcpkg manifest/toolchain; enable both C and CXX in the consumer project
even when its own sources are C. Those dependencies remain private API details
but are necessary link inputs. The package preserves the producer's qpdf 12.4
static-dependency target normalization. PDFium remains a shared runtime in both
profiles. Its headers are not installed as part of the QuantaPDF API.

The bundled PDFium runtime stays beside the shared library on Unix platforms;
the installed QuantaPDF library uses a relative loader path. On Windows deploy
the installed runtime DLLs through the application's normal DLL search setup.
An application linking the static SDK owns its executable deployment and loader
paths. The package does not download dependencies, copy DLLs into consumer
projects, install language bindings or choose a global SDK location.

## Ownership and deterministic cleanup

Every successful factory publishes one owned handle, consumed exactly once by
its matching drop. A copied pointer is an alias, not a retained reference.
Passing NULL to the documented drops is safe; a non-NULL stale or double-dropped
handle is invalid. Drops complete synchronously on the calling thread. Bindings
must serialize their drops with all other QuantaPDF calls, including finalizer
activity, because ABI v2 requires serialization across the entire process.

| Value | Owner and release | Required lifetime |
| --- | --- | --- |
| `quantapdf_context *` | Caller reference: `quantapdf_drop_context`. | Published handles retain the domain. Caller release does not close them; the consumed context pointer must no longer be used. |
| `quantapdf_document *` | `quantapdf_close`. | Outlives its pages, rendered bitmaps and image snapshots. |
| `quantapdf_page *` | `quantapdf_drop_page`. | Borrows the document. |
| `quantapdf_bitmap *` | `quantapdf_drop_bitmap`. | Does not borrow the source page/image snapshot after construction; still follows its document-lifetime requirement. |
| `quantapdf_image_page *` | `quantapdf_drop_image_page`. | May outlive its source page, but not its source document. |
| Text/link/outline/annotation/form snapshots | Matching `quantapdf_drop_*`. | Independent copied observations; may outlive the source page, document or edit. |
| `quantapdf_pdf_edit *` | `quantapdf_drop_pdf_edit`. | Owns an independent graph; the source document can close after successful begin. |
| `quantapdf_composer *` | `quantapdf_drop_composer`. | Owns copied inputs and resources. Resource IDs belong only to that Composer. |
| `quantapdf_output *` | `quantapdf_drop_output`. | Owns serialized bytes independently of its producer. |
| Returned `char **` storage | `quantapdf_free`, never the binding's allocator. | Detached, caller-owned bytes; survives all source and context drops. |
| Returned `const char **` / `const unsigned char **` views | Corresponding snapshot/bitmap/output owns the bytes. | Borrowed only until that owner drops. Do not free or retain a view past that point. |
| Info records, resource IDs, edit references | Caller value storage; no drop. | Values do not retain an owner. IDs/references are valid only under the originating owner's documented rules. |

Context retention protects the runtime domain, not a borrowed source document.
Bindings must represent both obligations. Keep a document owner reachable while
using a page/bitmap/image handle, even after releasing the caller's context.
Accessor views should carry an owner lifetime or be copied before returning to
unrestricted managed code. A future arena cannot reset storage beneath these
published values; allocator changes require a separate compatible contract.

Snapshot construction completes before publication. A published snapshot is an
immutable observation: later edits and later snapshots do not refresh or change
its values or invalidate its borrowed strings. Drop each snapshot independently;
even equal observations do not share a public lifetime. Internally, typed
snapshots use one owner protocol that releases their payload before their retained
runtime context. This preserves the image/document requirement in the table.
Metadata and plain-text extraction remain detached caller-owned byte results,
released with `quantapdf_free`, rather than acquiring a new snapshot handle.

Composer form/pattern builders borrow their temporary child only for the
synchronous callback. They cannot drop or keep it, destroy the parent, or
reenter an in-progress mutation. A callback may release a separately owned
context reference. The parent and child retain the domain throughout the call.
Foreign exceptions or panics must not unwind through a C callback; translate
failure to a `quantapdf_status` within the binding's callback boundary.

## Foreign type and string mapping

| C declaration | C++ | Rust FFI | Go/cgo | C# interop |
| --- | --- | --- | --- | --- |
| Opaque `quantapdf_* *` | Pointer to the declared incomplete type | Pointer to an opaque FFI type | cgo pointer to the native type | `IntPtr`/`nint` or an owner wrapper |
| `size_t` | `size_t` | `usize` on the matching target | `C.size_t` | `UIntPtr`/`nuint` |
| `int` / current enums | Declared C type | C-compatible integer carrier | `C.int` / generated C enum type | `int` on supported targets |
| `uint32_t` | `uint32_t` | `u32` | `C.uint32_t` | `uint` |
| `float` / `double` | Same C type | `f32` / `f64` | `C.float` / `C.double` | `float` / `double` |
| Public records | Include the C header | `repr(C)` with native field types | Generated native record | Sequential layout, native field order/alignment |

Do not use a closed Rust enum to receive future status/enum values without
handling unknown discriminants. Keep raw FFI declarations separate from safe
ownership wrappers. Rust layout and unwinding rules are described in the
[Rust FFI guide](https://doc.rust-lang.org/nomicon/ffi.html).

Strings and file paths use UTF-8. NUL-terminated input parameters need a stable
terminator; byte/string results with an explicit length must be decoded using
that length. NULL password and empty password are distinct inputs. Pin or copy
foreign input only for its documented borrow period. For C#, match native
signatures and manage the lifetime of callback delegates and native owners;
see [.NET native interop guidance](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/best-practices).

Go wrappers must follow the [cgo pointer rules](https://pkg.go.dev/cmd/cgo#hdr-Passing_pointers)
for any input or callback userdata. A `void *` is not permission to retain an
arbitrary Go pointer in C. Use cgo-compatible storage/handle indirection where
needed. Binding implementation and generated declaration tests belong to #169.

## Errors, structure sizes and version selection

`QUANTAPDF_OK` is success; otherwise use the returned `quantapdf_status`.
`quantapdf_status_string()` returns a borrowed process-lifetime diagnostic string.
Do not depend on errno, GetLastError, a C++ exception or a global last-error slot.
Do not consume outputs after failure. Factory output slots are reset to NULL;
other records/scalars follow their individual reset rules, so a binding must
not infer success from a nonzero field. Unknown status values remain errors.

Initialize option/info records to zero, then set `struct_size` to the matching
native layout size or documented version size. Single records use append-only
size negotiation. Fixed-stride array elements cannot grow without a new
type/API or an explicit stride: follow their published V1 size ranges. A larger
foreign buffer is not evidence that the library accepts a newer array layout.
Padding must follow the target ABI; do not compute a record size by summing
field widths or by using a host language's packed serialization size.

The header version macros describe the header being compiled. They are not a
query of a loaded DLL. CMake's package version file accepts the requested minimum
within the same major version and rejects a different pointer width. Consumers
must select a matching OS/architecture and configuration as well; pointer width
alone cannot distinguish architectures. An application using a function added
in 2.34 must deploy a library exporting that function, not merely any ABI-2 DLL.
Foreign loaders must check required symbols before use. There is no runtime
version-query or capability-negotiation API yet; #172 owns the latter design.

Within ABI v2, existing symbols, numeric enum values, record fields, handle
ownership and error meanings remain compatible. Additive functions or accepted
enum values can arrive in a minor release; fixes that preserve that contract can
arrive in a patch release. Deprecation must document a replacement, migration
and tests while retaining the old entry point for the ABI major. Removal, changed
calling convention, incompatible record layout or changed ownership requires a
new ABI and shared-library major version. Never reuse an old symbol or enum value
with a different meaning.

## Decisions, verification and limits

The package exports one stable C-facing target and records the private static
link closure. Shipping only `quantapdf.lib`/`libquantapdf.a` would omit required
backend objects; publishing backend headers would turn implementation details
into an SDK contract. The selected model keeps those headers private and lets
CMake supply the required archives. Shared packages avoid imposing a C++ SDK
dependency on callers. Static packages trade that convenience for explicit
toolchain/dependency coupling. Both preserve existing source APIs and file data.

Migration is additive: select the installed package and replace manual include/
library flags with `QuantaPDF::QuantaPDF`. Keep the complete old installation
available for rollback; switch the consumer's configured SDK root back rather
than mixing old libraries with new headers. Reverting package metadata does not
require a PDF migration, but removing a published package target is a consumer
build break and must follow the compatibility policy.

Verification uses the existing public C/C++ behavior suites, context lifetime
tests, version checks, real shared export-table checks, and normal preset
configure/build/install steps. Package generation is not proof that every
language binding has been tested. No generated Rust/Go/C# bindings, dynamic
capability API, concurrency guarantee or aggregate allocator quota is shipped
by this contract. Platform CI and sanitizer results must be reported for the
actual commit rather than inferred from this design.
