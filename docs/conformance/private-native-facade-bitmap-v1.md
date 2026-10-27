# Private Native C facade Context/Bitmap qualification V1

Tracks [Issue #455](https://github.com/qigao/QuantaPDF/issues/455)
and the eventual PDFium-removal exit [#208](https://github.com/qigao/QuantaPDF/issues/208).

## What is connected

The existing product already has a private strict chain:

```text
built-in Page -> complete Source V8 -> detached owned IR
    -> C11 opaque RGBA8 -> C11 owned RGB8 backend bitmap
```

The new **private** `quantapdf_render_page_native_qualified_v1()`
connects this to the **actual C facade** `quantapdf_page` and its
retained `quantapdf_context` domain:

```text
quantapdf_open_with_context -> quantapdf_load_page
  -> private Native Page/Source/IR/C11 RGB
  -> quantapdf_bitmap (same internal struct as public API)
  -> quantapdf_bitmap_dimensions / quantapdf_bitmap_data
  -> quantapdf_drop_bitmap
```

The private facade function is compiled into canonical PIC
`quantapdf_native_facade_objects` in the top-level CMake and embedded
into the product's `quantapdf` library. Likewise the existing Context
retain/allocator helpers use `quantapdf_context_lifetime_objects`.
Those **same object files**, with matching test-mode Context layout,
are also linked directly into the in-tree Windows original-source oracle.
This ensures that internal tests exercise the actual production code
without exporting hidden facade or Context helper symbols from the
shared DLL. Both private headers are **non-installed**. There is **no
public ABI/export** or C11 SDK function declaration added. The normal
public `quantapdf_render_page`, `quantapdf_render_page_with_options`
and `quantapdf_render_thumbnail` are unchanged and still invoke
PDFium through `builtin_ops.render_page`. This is not a cutover or
Native-fallback option.

## Ownership and fail-closed behavior

1. Validate output slot, facade Page/Context/Document identity, built-in
   provider selection and selected page index. Non-built-in pages,
   mismatched pages and unqualified Source/device inputs refuse.
2. Retain exactly one Context reference before using the private
   context allocator, exactly as public rendering does.
3. Confirm the selected page's UserUnit is 1.0; the original Source V8
   adapter admits only an integer-pixel, page-zero, 72-DPI, zero-rotation,
   opaque, un-clipped-by-device and non-thumbnail profile.
4. Invoke the already-qualified real built-in Page Native RGB backend
   producer; borrow no temporary IR/QPDF/provider object beyond its
   complete invocation. On success only, transfer its independent
   malloc-owned RGB byte buffer into the new `quantapdf_bitmap` with
   Context guard and the existing width/height/stride/components.
5. After success the bitmap stays live when the Page, Document, caller's
   Context reference, and original on-disk PDF are released. Existing
   `bitmap_dimensions`, `bitmap_data`, `drop_bitmap` continue to
   work without any special Native-specific destructor.
6. On every error, including missing/invalid arguments, unsupported
   source/operator/resource, wrong page/DPI/UserUnit, source or raster
   work budgets and context allocation failure, publish NULL output
   and free any intermediate RGB samples, bitmap allocation and the
   retained Context reference. **Never** call legacy PDFium as fallback.

## Evidence

`quantapdf.private_native_facade_bitmap_v1` creates original QPDF
source bytes, writes them as an ephemeral fixture, and invokes the real
public facade `quantapdf_context_create`,
`quantapdf_open_with_context`, `quantapdf_load_page`, followed by
the **private** Native facade function.

Its accepted original PDFs cover 2/3/4 ordered Path/Image paints,
single W/W* frozen Clip, genuine Q restoration, V7 normalized RGB,
V8 DeviceGray and split Contents. Compare both with independent,
unchanged-original **test-only PDFium** pixels and the existing
PDFium-backed public `quantapdf_render_page` bitmap metadata/pixels;
neither is used as a fallback inside Native.

Drop Page and Document and the caller's original Context reference
**before** reading the two detached `quantapdf_bitmap` objects.
Validate both still have live exactly-counted Context owners, exact
RGB metadata and pixel values, then dispose them with the ordinary
public bitmap destructor.

In tests linked against the Windows DLL, the page's real immutable
Context and Document provider-table pointers are compared **to each
other** rather than to a duplicated static backend table from another
module. UserUnit is obtained using the already-bound `page_user_unit`
provider callback. Both are equivalent to the existing facade's
built-in owner contract for admitted pages, without exporting private
symbols. The full provider remains fixed at Context creation; this
does not authorize an alternate backend or public Native dispatch.

The test additionally repeats late unsupported Text, SMask/missing
resource, source and raster quota exhaustion, incorrect page index,
DPI and alpha, poisoned output clearing, unchanged Context refcount
and valid post-failure rendering. The existing product-only static
link canary also directly references the private facade function.

## Acceptance / non-goals

Require exact-head Linux Release + ASan/UBSan, macOS and Windows,
standalone C11/backend Native conformance, Installed SDK, Windows
public DLL export baseline, original PDFium pixel oracle and the strict
formal external corpus (**7 licensed originals / 2 independently
W* Native-positive**). Verify separate post-merge master CI.

Do not switch public rendering, remove PDFium runtime/import/install
dependencies, expand Native Raster's four-operation cap, infer
text/font/Form/transparency/general device support or close
#208/#145/#154/#214/#427. The facade integration is solely an
internal Context/bitmap-lifetime qualification.
