# Private built-in Page → Native Source/Raster qualification V1

Tracks [#451](https://github.com/qigao/QuantaPDF/issues/451),
[#449](https://github.com/qigao/QuantaPDF/issues/449) and
[#208](https://github.com/qigao/QuantaPDF/issues/208).

## Exact owner integration

This is an explicit, **private** alternate qualification path for the
existing built-in provider's opaque document/page handles. The built-in
backend already opens original PDF bytes through QPDF, exposes the
existing `quantapdf_backend_ops` table and lazily opens PDFium only
for its production `render_page` callback.

`quantapdf_builtin_native_qualify_page_v1()` accepts a real provider
page from `quantapdf_builtin_backend_ops()->load_page()`. It validates
the page/document handle, source byte lease and selected page and
then calls the already integrated, bounded
`quantapdf_native_source_raster_bridge_v1()`. No original-source
bytes are copied or persisted by the adapter, and the original page
owns no Native C11 output. Source-owned objects are destroyed inside
the existing complete Source→detached IR bridge before the C11 Raster
consumes them. Caller owns the detached returned RGBA8 image.

## Canonical static archive ownership

The static-link canary exposed a genuine archive-order cycle in the
original candidate: a bridge object in `quantapdf_backend` referenced
`quantapdf_ir_raster_scoped_mixed_v5` in the already-scanned static
`quantapdf` archive. The integration now uses a strict **single
product -> backend dependency direction** instead:

- `quantapdf_backend` alone owns the built-in page/document structure
  and supplies `quantapdf_builtin_page_source_view_v1()`, a checked,
  read-only borrowed {source bytes, password, selected index} record.
- A private C11 `quantapdf_native_bridge_objects` OBJECT target owns
  both `quantapdf_native_source_raster_bridge_v1()` and the page
  adapter `quantapdf_builtin_native_qualify_page_v1()`; the objects
  are included in the main `quantapdf` product target, not the
  backend static archive. In-tree oracles can embed those same compiled
  objects without creating new exported ABI symbols.
- `quantapdf_native_raster_objects` also belongs to the main product
  target; Source view and QPDF parser references resolve later in
  `quantapdf_backend`, eliminating reverse Raster references.
- `quantapdf.private_builtin_native_static_link` compiles a minimal
  private consumer that links **only** `QuantaPDF::QuantaPDF` (no
  explicit backend library or test-local Raster objects), testing the
  real Linux static archive order and private symbol closure.

This intentionally avoids CMake static link cycles, repeated archives,
`--start-group` fixes, duplicate Raster copies and public PDFium
rendering or SDK export changes.

The adapter **never calls** `builtin_ensure_legacy_page`,
`quantapdf_pdfium_open_memory`, or `quantapdf_pdfium_render_page`.
The existing `builtin_ops.render_page = builtin_pdfium_render_page`
remains entirely unchanged. Public page rendering, options, thumbnail
and SDK still use PDFium. This is **not** a production cutover,
fallback policy or new ABI entry.

## Strict fail-closed contract

- Only the existing Source V8 one-integer-pixel page, selected page 0,
  one frozen W/W* Clip, real Q-restored paint, 2–4 mixed PATH/IMAGE
  operations and 72 DPI device profile qualify.
- Both page handle index and requested device page index must match,
  or UNSUPPORTED is returned before invoking Source or legacy render.
- Password-bearing provider sessions explicitly refuse: this first
  native Source bridge does not supply passwords to its independent
  complete original-PDF parser. No credential is silently ignored.
- Invalid/null provider handles or inputs return ARGUMENT, and
  unsupported device/Source/Clip/color/paint work returns the status
  of the existing bounded bridge.
- Every failure zeroes the output before any source/device operation
  and does not corrupt the borrowed built-in page/document. Neither
  handle is retained by Native; page is dropped before document close.

## Evidence

`quantapdf.native_builtin_page_owner_v1` uses the **real product**
`quantapdf_builtin_backend_ops()` to `open_memory`, `page_count`,
`load_page`, `page_bounds`, qualify Native, `drop_page`, and
`close`, twice from the same original source bytes. The original
PDF bytes are separately opened by test-only PDFium for full-page RGB
and fixed red/blue/gray/white probes; neither PDFium nor an IR→PDF
re-export participates in the Native path.

Tests cover 2-, 3-, 4-operation source ordering, V7 normalized RGB,
V8 DeviceGray, frozen W/W*, split Contents, Q restores, detached
RGBA8 pixel and stride invariants, exact repeat after teardown,
and rejected late text, extra Image Do, SMask, missing resources,
operation/pixel budgets, invalid device/page flags and null page.
After each negative, the same built-in page's bounds remain readable;
a later valid source still paints successfully.

The previous source-V1–V8 profile, private byte-level bridge and
standalone provider-free C11 raster tests remain enabled and are not
weakened.

## Gate and exclusions

Draft only until Linux Release+ASan/UBSan, macOS, Windows, product
conformance, installed SDK, original-source PDFium oracle, strict
exports baseline and licensed external corpus reconciliation all pass
on the same HEAD. The formal independently licensed corpus stays
**7 original PDFs / 2 W* Native pixel positives**; generated tests do
not change that count.

Do not change the public renderer or installed PDFium package.
Broader native font/text/Form/transparency/Clip/device coverage and
production PDFium removal remain under #208/#145/#154/#214/#427.
