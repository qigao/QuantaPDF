# #269 — NativeDeviceGrid v0 (private geometry-only stage)

Status: candidate private C11 module, **not an enabled native raster path**.
This is stacked on #273 (independent original-PDFium coordinate API oracle),
which passed Linux Release + ASan/UBSan, macOS and Windows CI.

## Ownership and supported scope

`src/render/native_device_grid.{h,c}` has no heap allocations, provider
headers, borrowed handles, callback or public ABI. It derives one immutable,
copyable device-grid value from a **double-precision displayed page extent**.
Inputs explicitly carry zero normalized visible origin, UserUnit=1, zero
intrinsic/requested rotation, and 72/96/144-DPI; anything else fails closed.
The module does not perform raw PDF CropBox-to-display normalization.

Dimensions follow the observed integer PDFium draw rectangle:
`ceil(display_width_points * (dpi/72))` by
`ceil(display_height_points * (dpi/72))`. The original jsPDF 595.28 x 841.89
point geometry gives 596x842 @72dpi, 794x1123 @96dpi and 1191x1684 @144dpi.
Original extents are never rounded or rewritten. The per-axis mathematical
inverse maps subpixel centers as
`(device_index + (sub_index+0.5)/4) * points_per_device_axis`.
This does **not** claim actual PDFium antialiasing-center behavior.

## Bounded admission and refusal

Before grid publication, enforce finite positive input, positive and finite
per-axis inverse mapping, signed-int-compatible output extent, a pixel ceiling,
a separate 4-channel RGBA byte ceiling, and 16 base sample slots per pixel.
Callers must additionally bill every paint and Clip edge against a global
edge limit and sample-test budget. Division-first comparisons prevent
multiplication overflow. Failure clears the entire output value, and this
module never allocates, writes pixels, or touches provider handles.

The float-backed Detached IR precision gate requires exactly matching device
extent and no more than 1/4096-device-pixel drift on either page axis. It
rejects the known 595.000001 to 595.0 narrowing counterexample. Passing it
does **not** verify individual path coordinate precision or native pixel parity.

## Tests and remaining gates

The independent `quantapdf.native_device_grid` CTest compiles the actual
private C11 source, without PDFium/qpdf. It covers the three DPI geometries,
repeat determinism, samples, all four bounds, small exact over-budget cases,
invalid offset/rotation/UserUnit/precision and full zero-output rejection.
Only one CTest is added, with no formal corpus or W* positive increment.

No call site is added to `ir_raster_v1.c` or
`ir_scoped_operations_v1.c`. Their existing integer-only page rejection and
the SHA-pinned original jsPDF typed-lowered / UNSUPPORTED / zero RGBA negative
must remain unchanged. Native raster enabling later needs Path/Clip/stroke
transform integration, full original-PDFium image parity, precision and
bounded work gates, and full cross-platform CI. Public SDK stays on PDFium;
#208 cutover remains blocked.
