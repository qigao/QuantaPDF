# Private Native RGB backend-bitmap layout V1

Tracks [Issue #453](https://github.com/qigao/QuantaPDF/issues/453),
[#451](https://github.com/qigao/QuantaPDF/issues/451), and production
PDFium-free exit [#208](https://github.com/qigao/QuantaPDF/issues/208).

## Exact purpose

`quantapdf_builtin_native_qualify_bitmap_v1()` is a **private** product
qualification entry: real built-in provider Page -> strict Source V8
admission -> complete detached owned IR -> existing pure C11 Raster RGBA
-> separately owned RGB8 `quantapdf_backend_bitmap`.

The source of truth for Page/Source admission remains
`quantapdf_builtin_native_qualify_page_v1()`. Its existing page-zero,
72-DPI, no-rotation/no-region-Clip/no-transparent/no-thumbnail profile
and complete W/W*, q/Q and operation budgets are **unchanged**.

The conversion stage is pure C11 and provider-free:
`quantapdf_native_rgba_to_backend_rgb_v1()`. Its source includes no
qpdf/PDFium headers or API calls and is compiled **once** in a dedicated
private PIC OBJECT target. The exact same object is reused by the
product and the C11 standalone and original-source pixel oracle tests.

## Bitmap and ownership contract

- Input is a borrowed, nonzero tightly packed row-major, top-left RGBA8
  image. Require pixel count <= caller's max and the existing hard bound
  of 1,048,576. Verify width/height/stride/byte-size arithmetic against
  `SIZE_MAX` and `INT_MAX` before any pixel read, conversion or
  allocation.
- Require every source alpha byte to be **exactly 255**. No silent
  flattening, premultiplication, alpha channel fallback or blend
  approximation.
- Result: `data` is separately `malloc`-owned RGB8 (caller
  `free`), `components=3`, `width` and `height` are bounded
  `int`, `stride=width*3`, `size=stride*height`, and all bytes are
  direct copies of source red/green/blue in order.
- Every failure (including malformed source, invalid device, quota,
  allocation failure, nonopaque alpha, wrong stride/size, null
  argument) publishes a completely zeroed backend bitmap. The
  temporary native image and owned IR are disposed on success/error;
  the borrowed document/page is not retained or modified.
- No qpdf/PDFium call inside the converter or Native path; the
  independent test oracle may use PDFium, but the bridge never retries
  an unsupported case using a legacy renderer.

## Evidence

`quantapdf.native_backend_rgb_v1` is a **standalone pure C11**
conformance executable with only the canonical RGB converter OBJECT:
no QPDF/PDFium/provider link. It checks exact RGB bytes, detached
ownership after RGBA destruction, repeated success, fully zeroed
poisoned output on wrong budgets/stride/size/alpha/overflow and
post-failure recovery.

`quantapdf.native_builtin_backend_rgb_v1` exercises actual
`quantapdf_builtin_backend_ops()->open_memory/load_page` ownership,
renders admitted original PDFs through the **private** Native RGB
adapter and independently compares with unchanged-original direct
test-only PDFium and the existing PDFium provider's RGB bitmap. Tests
cover 2/3/4 mixed Path/Image paints, V7 normalized `rg`, V8
DeviceGray `g`, W/W*, q/Q and split Contents, deterministic repeated
owned buffers, strict whole-page/golden probes and late failure.

The `quantapdf.private_builtin_native_static_link` C11 executable
also calls the RGB converter and private bitmap owner adapter by
linking **only** the actual QuantaPDF product library, ensuring they
resolve with the existing one-way product-to-backend static archive
closure and without test-local Raster/adapter OBJECT duplicates.

## Deliberate exclusions and merge gates

No public `quantapdf_render_page()`, render options or thumbnail
rerouting. Existing `builtin_ops.render_page` remains PDFium-backed.
No public C11 exports/headers, installed PDFium import/runtime
removal or extra Source/Raster capacity. This private output-layout
slice is **not** evidence for arbitrary PDF compatibility.

Require exact-head full Linux Release/ASan+UBSan, macOS, Windows,
Installed SDK and Windows DLL ABI exports; unchanged-original PDFium
pixel comparison; strict external licensed corpus reconciliation.
The formal independent external source corpus remains **7 originals /
2 W* Native pixel positives**; generated test PDFs do not change
that count. Track public backend replacement under #208 separately.
