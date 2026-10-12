# QuantaPDF product conformance (Issue #173)

QuantaPDF's product gate is **CTest**, exercised from the project's pinned
CMake presets. The gate tests externally observable semantics and ABI; it is
not a replacement for the complete development test suite.

## Commands

After building the selected preset:

```sh
ctest --preset linux-release-user -L conformance --output-on-failure
ctest --preset linux-dev-user -L conformance --output-on-failure
ctest --preset win-release-user -L conformance --output-on-failure
# On macOS use the configured macos-*-release-user preset.
```

`.github/workflows/ci.yml` runs the conformance label after the complete
CTest suite on Linux Release, Linux ASan/UBSan, Windows and macOS. Windows and
macOS PR jobs require the `full-ci` label; both also run on master pushes.
A missing named test fails CMake configuration rather than silently reducing
the product gate.

## Coverage contract

| Area | Registered tests | Product assertion |
| --- | --- | --- |
| ABI / error contract | `product_conformance`, `version`, `status`, `abi_exports` (when registered) | Public C11 header, ABI major, status/reset, export surface |
| Document lifecycle | `context`, `document`, `graph_snapshot`, `graph_edit` | Ownership, publication and snapshot independence |
| Read and raster | `render`, `text`, `structured_text` | Observable page rendering and extraction |
| Determinism | `product_conformance`, `pdf_rewrite_lossless`, `pdf_export` | Repeated byte-equal rewrite within one SDK and retained output bytes |
| Security / malformed inputs | `product_conformance`, `pdf_security`, `pdf_security_rewrite`, `validation` | Failure-atomic publication, rejected invalid input, sanitization policies |
| Image / Composer / IR | `pdf_image_recompression`, `composer`, `pdf_clip_pixels`, `pdf_extgstate` | Real PDF round trips and rendering comparisons of supported features |

The separate `tests/test_product_conformance.c` links **only** the public
`QuantaPDF::QuantaPDF` target, not PDFium/qpdf or private project headers. It
tests rejection without publishing handles, byte-stable repeated lossless
rewrites of a checked-in PDF, and output ownership after the source closes.
Other registered suites contain the larger negative PDF corpus, bounded work
and object-graph semantics. Do not turn a deliberately unsupported PDF
construct into a success by applying an unverified fallback.

## Acceptance and evidence

1. Verify the exact pull request/commit SHA and installed SDK identity.
2. Require complete Release CTest, conformance label, and SDK install on all
   three platforms. Linux also requires the ASan/UBSan configuration.
3. Verify installed C11 Composer consumers on Windows and the Poppler
   qualification on Linux. Keep backend and compiler diagnostics visible.
4. Record any unsupported content categories separately; **a green gate is
   not a claim of arbitrary PDF import, full raster IR, or PDF/A compliance**.
5. Retain the raw Actions links and test counts in the integration PR before
   merge. A branch-only check does not substitute for master push validation.

## Not yet certified

This first independent product-level matrix does not certify generated
C#/Rust/Go/Python bindings, multi-threaded SDK usage (the SDK requires caller
serialization), PDF/A or PDF/UA standards compliance, untrusted-input process
sandboxing, arbitrary PDF content-to-IR conversion, or universal cross-platform
byte-identical output. Those need separate executable fixtures, golden corpora
and explicit capability/ownership contracts before marking them qualified.
