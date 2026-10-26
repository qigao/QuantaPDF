# Private product-archive original Source → C11 Raster bridge V1

Tracks [#449](https://github.com/qigao/QuantaPDF/issues/449),
[#427](https://github.com/qigao/QuantaPDF/issues/427) and
[#208](https://github.com/qigao/QuantaPDF/issues/208).

## Implementation and linkage

This stage **does not change** the production `quantapdf_render_page()`,
`quantapdf_render_page_with_options()`, or thumbnail dispatch. Those
continue to use the existing PDFium-backed built-in provider.

The new private
`quantapdf_native_source_raster_bridge_v1()`, compiled into
`quantapdf_backend`, composes the already-qualified existing
`quantapdf_qpdf_source_scoped_mixed_v8_to_ir()` with
`quantapdf_ir_raster_scoped_mixed_v5()`:

```text
Original PDF bytes
    |
    v
Private complete Source V8 admission (QPDF, bounded)
    |
    v
Uniquely owned detached one-page Composer IR
    |      Source/QPDF owner already closed
    v
Provider-free C11 mixed Path/Image Raster (bounded)
    |
    v
Uniquely owned RGBA8 / zeroed output on any failure
```

The former test-only `ir_raster_scoped_mixed_v5.c`,
`ir_raster_image_clip_v1.c` and `ir_raster_image_v1.c` are
now compiled **once** into private
`quantapdf_native_raster_objects` CMake OBJECT target, with
`POSITION_INDEPENDENT_CODE` and strict C11 warnings. Exact object files
are embedded in `quantapdf` and its dedicated original-source/standalone
raster tests, instead of recompiling those same files separately in
the affected targets. Underlying previously integrated
`ir_raster_v1.c`, `ir_raster.c` and
`native_device_plan.c` remain unchanged and continue to be supplied
from their existing product/test source arrangements.

There is no new installed CMake target, no source ownership cycle,
no second C11 source implementation, no public ABI export and no
PDFium/qpdf dependency of the C11 OBJECT target. QPDF remains
only in the source interpreter, and PDFium only in test oracle or
the untouched legacy production renderer.

## First device contract

The bridge accepts only:
- One original page, indexed zero, with existing V8 bounded 2–4 source
  ordered PATH/Image operations, 1–2 of either kind, exactly one frozen
  W/W* leaf and a real Q restoring unmasked paint.
- 72 DPI, zero additional rotation, no public clip region, no
  transparent-page output and no thumbnail rendering.
- Explicit independent original-PDF byte, decoded-image, operation,
  drawn-pixel, C11 output pixel, edge, sample-work and image quotas
  through the private composite `limits` structure.

Everything outside that source/device profile returns
`QUANTAPDF_ERROR_UNSUPPORTED` rather than silently falling back to
PDFium. Invalid input pointers, flags or non-finite device options
return `QUANTAPDF_ERROR_ARGUMENT`. Every non-null output is reset
**before** argument/admission checks. Source and Raster errors are
propagated unchanged, and the detached IR is disposed exactly once
after any admitted invocation, regardless of the result.

This bridge is *not* yet a general public render interface: it
doesn't support source text/font/Stroke/Form, complex colors, arbitrary
Clip DAG, alpha/transparency, device transforms, thumbnails or multiple
selected pages.

## Direct original-PDF evidence

`quantapdf.native_source_product_bridge_v1` invokes the real private
bridge from the product-linked backend archive, not a duplicate test
implementation. Original deterministic QPDF-authored bytes are
rendered **independently and unchanged** with direct test-only PDFium
(no IR re-export oracle), and compared to native full-page RGB and
red/blue/gray/white interior probes.

The dedicated gate covers:
- Valid 2/3/4 ordered Path/Image layers with frozen W, real Q, fractional
  RGB V7 and normalized DeviceGray V8, plus translated W* clip
  definition and split Contents streams.
- Exact byte repeats across full Source/QPDF document destruction and
  reopening, RGBA width/stride/ownership and opaque background pixels.
- Late unsupported Text, excess Do, SMask, missing referenced resource,
  Source operation quota exhaustion, Raster operation/pixel exhaustion;
  repeated poison-output reset and clean post-failure recovery.
- Invalid or unqualified device page index, dpi, rotation, clip,
  alpha and thumbnail flags with zero output and no legacy fallback.

All previously admitted Source V1–V8 CTests, including the twelve-mode
profile parity and the pure-C11 standalone V5 test, remain enabled.

## Strict acceptance / remaining blockers

This is a **Draft-only candidate** until the exact HEAD passes Linux
Release + ASan/UBSan, macOS, Windows, original direct-PDFium pixel
tests, Installed SDK (static/shared), symbol/exports isolation and
strict formally licensed external-source reconciliation.

The official corpus remains **7 unchanged external originals /
2 independent W* Native pixel positives**. Generated source test
fixtures are not added to the licensed corpus.

Production page render/thumbnail, installed PDFium import/link/package
dependencies and full #208 cutover remain **unchanged**. Keep
#449/#427/#145/#154/#214/#208 OPEN until their full feature,
ownership and installed SDK gates qualify separately.
