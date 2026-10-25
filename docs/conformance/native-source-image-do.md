# Original Image XObject to owned Native RAW Image IR — bounded slice

Tracks [#427](https://github.com/qigao/QuantaPDF/issues/427),
[#145](https://github.com/qigao/QuantaPDF/issues/145),
[#154](https://github.com/qigao/QuantaPDF/issues/154) and
[#208](https://github.com/qigao/QuantaPDF/issues/208).

## Contract

The new **private** `quantapdf_qpdf_source_raw_images_to_ir()` accepts
original immutable source PDF bytes and returns a **one-page, uniquely
owned Composer IMAGE IR** only when it understands **all** page content.

- Source structure comes from the existing qpdf private provider. The
  original page tree, inherited `/Resources`, explicitly selected
  Image XObjects and ordered `/Contents` are used; no native IR export is
  performed before the reference is rendered. The referenced ancestor
  chain is separately bounded by `max_parent_steps` (hard cap 256),
  rejects cycles and malformed leaf overrides instead of falling back to
  a valid ancestor, and is independent from Content Stream limits. The
  qpdf source owner ends inside the function; a successful IR outlives
  all source objects.
- First accepted page geometry is the positive integer `[0 0 W H]`
  MediaBox/CropBox, rotation zero, UserUnit=1 and no group or annotations.
- The **complete** content stream grammar is restricted to balanced
  `q/Q`, finite `cm` values and named Image `Do`. Streams in a
  `/Contents` array are concatenated in order and **q/Q can span
  their boundary**. Image CTMs must be positive, axis-aligned and
  pixel-aligned, with page-contained integer STRETCH bounds.
- Referenced images must be indirect or direct PDF streams of Subtype
  `/Image`, precisely 8-bit DeviceGray/DeviceRGB with no image mask,
  soft mask, custom Decode, ICC/Indexed/CMYK, inline image or Form.
  Only unfiltered RAW and explicitly Flate-decoded streams without
  predictor parameters are accepted. Decoded image samples are copied
  to **owned** RAW Composer image resources (no borrowed QPDF buffers).
- The private output is consumed by the already merged pure-C11
  `quantapdf_ir_raster_raw_images_v1()` (PR #426), which does not
  link or call PDFium/qpdf.

A late Text, Path, ColorSpace, Form, shading, inline image, unsupported
graphics state, invalid image reference, out-of-page CTM or capacity
overflow aborts the **whole page**, not simply the unsupported draw.
Missing or malformed resource references return FORMAT; unimplemented
well-formed features return UNSUPPORTED. In both cases all output IR
fields are reset and no partial bitmap/resource becomes visible.

Caller budgets further restrict a maximum 2 MiB encoded source, 64 KiB
decoded content, four streams, four Image draws, 4,194,304 decoded image
pixels and 4,194,304 page pixels with 16,777,216 drawn pixels.

## Conformance

The generated original-PDF test `quantapdf.native_source_image_do`
renders the **unchanged original bytes** through independent test-only
PDFium for full-page RGB comparison with the Source→detached-IR→C11
Native image, interior color/orientation goldens and repeated RGBA.
The test also exercises nested `q/Q`, Content Stream boundaries,
overlap, multiple/recurring XObject names, **parent-owned indirect
/Resources** with insufficient/sufficient ancestry budgets, invalid leaf
resource shadowing, resource failure after earlier valid operations,
malformed image states, late unsupported operators, poisoned out pointers,
byte/geometry capacity and successful recovery.
It does **not** add the generated fixtures to the formally licensed
external corpus, which stays at **7 originals/2 W* positive**.

The standalone `quantapdf.native_ir_raw_images_v1` stays pure C11 with
**zero qpdf/PDFium link dependency**. The new source-oracle test *may*
link qpdf and PDFium because it is a test-only independent reference.

## Not yet supported

This qualification is **qpdf-backed private source admission**, not
general Graph Snapshot lowering. It does not implement arbitrary
text/font or image/form rendering, shear/rotation, `CONTAIN/COVER`,
complex colors/filters/masks, Clip mixing, transparency groups,
annotations, full PDF compatibility or production public API cutover.

The production `quantapdf_render_page()` and thumbnail still dispatch
through PDFium until #208's complete feature and installed-SDK
dependency gates are satisfied. Extending the Graph source IR and
mixed Path/Image/Clip layers remains #145/#154 follow-up.
