# PDFium production removal — native-only architecture

Implemented in [PR #468](https://github.com/qigao/QuantaPDF/pull/468).
The objective is **deletion before feature parity**, not keeping PDFium
until comparisons are good enough.

Production now uses only QPDF for document structure/decoding, a uniquely
owned internal IR for admitted Source operations, pure-C11 native Raster,
and Context-owned RGB public Bitmap output. The pinned PDFium provider
files, CMake downloader/import target, runtime and SDK installation were
removed. Public `render_page` invokes the V10 native path.

This is an explicit breaking feature-coverage change:
- Native source currently admits one unencrypted page-zero with UserUnit
  1, 72dpi, opaque/zero rotation/zero region option, source-ordered
  2–4 Path+Image paints, one frozen W/W* Clip and actual Q restoration.
- Valid no-paint source pages now produce white RGB through the same
  V10→IR→C11 Raster→Context Bitmap pipeline, with no fallback.
- 8-bit DeviceGray/RGB RAW or bounded Flate TIFF/PNG Predictor values
  and per-channel exact Decode inversion are the admitted image subset.
- Native thumbnails for admitted 72-DPI pages are available through
  bounded area-averaged RGB reduction, with no upscaling or fallback.
  Their output retains the original Bitmap Context lifecycle.
- General fonts/text, complex Clip/Stroke, forms, masks, transparency,
  annotation appearances, arbitrary nonzero pages, DPI/rotation/device
  scale modes remain **UNSUPPORTED** until implemented.
- Unsupported content returns `QUANTAPDF_ERROR_UNSUPPORTED`, not an
  incomplete bitmap or any legacy/reference renderer fallback.

The new CI compiles and tests solely native C11 Raster/Clip/Device/Bitmap
and the real **public** native renderer against fixed RGB goldens. It
does not use PDFium binaries, downloaded oracle archives or cross-engine
visual comparisons. The installed package is audited for no PDFium link,
library, runtime or imported targets.

**Next work:** native feature implementation, prioritizing blank and
PATH-only pages, source geometry and device rotation/DPI, text/font
painting, general images and masks, complex Clip/strokes, Forms,
transparency and annotations. Each has strict deterministic pixel and
error/ownership/budget gates. Track in #208.
