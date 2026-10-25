# Native Path / RAW Image mixed raster V1

Private pure-C11 follow-up to [#154](https://github.com/qigao/QuantaPDF/issues/154),
[#145](https://github.com/qigao/QuantaPDF/issues/145),
[#427](https://github.com/qigao/QuantaPDF/issues/427) and
[#208](https://github.com/qigao/QuantaPDF/issues/208).

## First mixed-image paint milestone

`quantapdf_ir_raster_mixed_path_image_v1()` borrows owned Composer IR
and consumes **exactly two source-ordered operations**: one opaque,
solid-filled PATH plus one RAW Gray/RGB IMAGE with optional independent
Alpha. The operations may appear in **either order** and the result
changes accordingly at their overlap.

The IR need not be flattened into PDF and sent back into PDFium.
Internally the test-only mixer invokes two independently qualified,
pure-C11 renderers, isolating the PATH and IMAGE resources for each
layer, then composites their detached transparent RGBA8 results in
original operation order. Straight-alpha source-over uses deterministic
integer arithmetic, and the destination inherits the original page's
opaque or explicitly transparent background.

The selected private profile is deliberately bounded: one positive
integer-size page, exactly two operations, no Clip or GS resources,
no soft mask, paint resource, Form, font, links or outlines.
The PATH must be an opaque solid fill with existing V1 geometry/affine
constraints; IMAGE must satisfy the existing RAW Image V1
normalized-plane admission, with page-contained integer STRETCH
bounds, optional alpha and nearest-center sampling.

The caller specifies independent limits for page pixels, PATH
edges/subpixel sample work and IMAGE source/destination pixel work.
The mixed output also has a hard cap of 1,048,576 pixels. A
failed image or PATH layer—including **a late unsupported operation**
after a previously successful layer—discards all intermediate pixels
and returns completely reset output. No source/Graph/provider lifetime
escapes through the result.

`quantapdf.native_ir_mixed_path_image_v1` is a C11-only standalone
CTest binary, with no qpdf or PDFium linkage. Goldens cover opaque
and transparent backgrounds, red PATH and half-transparent blue
IMAGE order reversal, exact repeated output, alpha, lifetime
detachment, resource IDs, fit modes, mixed Text, malformed image
buffers, clipping/GS rejections, and budget-based atomic failure
followed by successful recovery.

## Explicit limits

This does **not** implement mixed Path/Image lowering from an
original PDF Content Stream: the currently admitted Image-only Source
Do interpreter under #427 correctly rejects other operations rather
than pretending they were painted. Connecting mixed Source operations
to typed, renderable owned IR is a separate #145 milestone, with a
test-only **original PDFium** whole-page oracle and no implicit fallbacks.

It does not handle multiple PATHs, multiple images, combined Stroke,
Text, XObject Forms, arbitrary Blend, Clip resources, image
transforms beyond STRETCH or page device transforms. Public
`quantapdf_render_page()` and installed SDK still use PDFium;
#208 is not complete, and the official external source corpus remains
7 admitted originals/2 W* positive. Do not count generated test
scenarios as newly licensed source PDFs.
