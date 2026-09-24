# QuantaPDF

**QuantaPDF — PDF made easy.**

QuantaPDF is a compact native PDF kernel with a versioned C11 ABI suitable for
native callers and .NET P/Invoke.

> **Backends:** QuantaPDF uses pinned PDFium `154.0.8021.0` for document
> inspection, rendering, and extraction, plus qpdf `12.4.0` for structural
> transforms and editing. Both remain private implementation dependencies.

## Current v2 ABI

The v2 ABI uses the `quantapdf_*` symbol/type prefix and `QUANTAPDF_*`
constants throughout. `<quantapdf/quantapdf.h>` is the sole public header;
the previous namespace and compatibility wrappers are not retained.

Document lifecycle remains the root of the API:

```c
#include <quantapdf/quantapdf.h>

quantapdf_document *doc = NULL;
int pages = 0;

if (quantapdf_open("file.pdf", NULL, &doc) == QUANTAPDF_OK) {
    if (quantapdf_page_count(doc, &pages) == QUANTAPDF_OK) {
        /* use pages */
    }
    quantapdf_close(doc);
}
```

Page + Render adds opaque page and bitmap handles:

```c
quantapdf_page *page = NULL;
quantapdf_bitmap *bitmap = NULL;
int width, height, stride, components;

if (quantapdf_load_page(doc, 0, &page) == QUANTAPDF_OK) {
    if (quantapdf_render_page(page, &bitmap) == QUANTAPDF_OK) {
        quantapdf_bitmap_dimensions(
            bitmap, &width, &height, &stride, &components);
        /* inspect bitmap data */
        quantapdf_drop_bitmap(bitmap);
    }
    quantapdf_drop_page(page);
}
```

The current supported surface includes:

- document open / password authentication / page count / close
- opaque page load / drop
- page bounds plus MediaBox/CropBox bounds
- RGB and RGBA page rendering
- DPI/zoom, rotation, and page-space clipping
- aspect-preserving thumbnail rendering
- bitmap dimensions and borrowed sample access
- plain UTF-8 text extraction
- immutable structured-text snapshots and geometric text search
- page-image enumeration and image bitmap rendering
- URI/internal links, annotations, document metadata, and outlines
- immutable AcroForm snapshots and isolated PDF edit sessions
- page export/range export, output merging, and file saving
- catalog-reachable document security audit and immutable policy sanitization
- explicit AES-256 encrypt, authenticated decrypt, and re-encrypt transforms
- immutable CropBox crop, MediaBox trim, poster-split, interactive-content
  flattening, lossless rewrite/GC, and selective image recompression transforms
- deterministic PDF composition from Base-14 text, direct-cmap embedded
  fonts, caller-shaped positioned glyph runs, JPEG/PNG images, opaque
  stroked/filled vector paths, a bounded static SVG subset, vector
  Code 128B/Code 39/EAN-13/UPC-A/EAN-8/UPC-E/QR barcodes, URI/internal links,
  and hierarchical outlines
- stable status strings and one allocator-matched `quantapdf_free()` entry point

## API contract

- The public header contains no PDFium or qpdf types.
- Backend handles and C++ exceptions remain private to the library.
- `quantapdf_page` and `quantapdf_bitmap` borrow their parent document. The document must outlive all derived page and bitmap handles.
- A bitmap does not borrow its source page after rendering, so the page may be dropped before the bitmap, provided the document remains alive.
- PDFium process state is private, initialized once, and serialized by the
  backend runtime because PDFium's public API is not thread-safe.
- Input paths are UTF-8.
- `password == NULL` means no password was supplied.
- Missing or incorrect passwords return `QUANTAPDF_ERROR_PASSWORD`.
- `quantapdf_open` accepts PDF input; non-PDF data returns
  `QUANTAPDF_ERROR_FORMAT`.
- `quantapdf_open` leaves the output handle NULL on failure.
- `quantapdf_close(NULL)`, `quantapdf_drop_page(NULL)`, and `quantapdf_drop_bitmap(NULL)` are safe.
- qpdf and standard C++ exceptions are caught inside the private bridge and
  translated to `quantapdf_status`.
- ABI v2 requires external serialization: applications must not execute
  `quantapdf_*` calls concurrently, including calls that use different
  documents or otherwise unrelated handles.

Snapshot/output ownership is explicit:

- `quantapdf_text_page`, `quantapdf_link_page`, `quantapdf_outline`,
  `quantapdf_annotation_page`, `quantapdf_form`, and `quantapdf_output`
  own their copied observations and remain valid for their documented snapshot lifetime;
- pointers returned by snapshot string/data accessors are borrowed until the
  corresponding snapshot/output is dropped;
- strings returned through `char **` outputs are caller-owned and must be
  released with `quantapdf_free()`;
- `quantapdf_pdf_edit` owns a private PDF graph and never mutates its source
  `quantapdf_document`.
- `quantapdf_composer` copies text and image inputs at each successful add/draw
  call. Every successful `quantapdf_composer_finish()` returns an independent
  `quantapdf_output`; finish does not consume or freeze the composer.

## ABI-sized structures

The installed header publishes `QUANTAPDF_VERSION_MAJOR`,
`QUANTAPDF_VERSION_MINOR`, `QUANTAPDF_VERSION_PATCH`, and
`QUANTAPDF_ABI_VERSION`. ABI version 2 follows these compatibility rules:

- existing exported functions, enum numeric values, structure fields, and
  ownership rules are not removed, reordered, or reinterpreted;
- new functions and enum values may be appended;
- incompatible changes require a new ABI version and shared-library major
  version.

`struct_size` has two distinct contracts:

- Single option/info structures are append-only. A library reads or writes
  only fields covered by the caller-provided size.
- Structures traversed as C array elements have fixed V1 layouts because an
  array has no independent element-stride metadata. These currently include
  `quantapdf_page_crop`, `quantapdf_page_trim`,
  `quantapdf_page_poster_split`, `quantapdf_search_result`,
  `quantapdf_form_value_input`, and the fixed-layout
  `quantapdf_composer_glyph` array element. Sized array records keep their
  documented V1 `struct_size` range; `quantapdf_composer_glyph` publishes
  `QUANTAPDF_COMPOSER_GLYPH_V1_SIZE`. Their accepted layout is
  matching `QUANTAPDF_*_V1_MIN_SIZE` through `QUANTAPDF_*_V1_SIZE`; values
  larger than the fixed V1 layout are rejected. Future extensions require a
  new type/API or an API carrying an explicit element stride.

## Page coordinates

All public page rectangles use **displayed page space** rather than raw PDF object coordinates:

- the CropBox top-left is the page-space origin `(0, 0)`;
- x increases to the right;
- y increases downward;
- values are page points before render scaling (`72 points = 1 inch`).

`quantapdf_page_box_bounds(..., QUANTAPDF_PAGE_BOX_MEDIA, ...)` may therefore have negative coordinates when MediaBox extends outside CropBox.

This same page-space contract is intended for later text geometry, search quads, images, links, and annotations.

Intrinsic format-specific rotation metadata is intentionally not part of the generic Page API. Page bounds already describe displayed geometry, including `/Rotate` and `/UserUnit`; raw rotation metadata, if needed by callers, belongs in a later PDF-specific metadata surface. Rendering rotation is explicit and per-call.

## PDF composition

The Composer facade creates new PDFs without exposing qpdf or PDFium types.
Coordinates use the same top-left, y-down point space as the Page API:

```c
quantapdf_composer *composer = NULL;
quantapdf_output *output = NULL;
size_t page_index = 0;

quantapdf_composer_page_options page = {
    .struct_size = sizeof(quantapdf_composer_page_options),
    .width_points = 595.0f,
    .height_points = 842.0f,
    .background_argb = 0xffffffffu
};
quantapdf_composer_text_options style = {
    .struct_size = sizeof(quantapdf_composer_text_options),
    .font = QUANTAPDF_COMPOSER_FONT_HELVETICA,
    .font_size = 12.0f,
    .argb = 0xff202020u,
    .line_height_multiplier = 1.2f,
    .alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT,
    .wrap = 1
};
quantapdf_rect text_box = { 36.0f, 36.0f, 559.0f, 180.0f };

if (quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK &&
    quantapdf_composer_add_page(composer, &page, &page_index) ==
        QUANTAPDF_OK &&
    quantapdf_composer_draw_text(
        composer, page_index, "Hello QuantaPDF", &text_box, &style) ==
        QUANTAPDF_OK &&
    quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK) {
    quantapdf_output_save_file(output, "created.pdf");
}
quantapdf_drop_output(output);
quantapdf_drop_composer(composer);
```

Register JPEG or PNG bytes once with `quantapdf_composer_add_image()`, then
place the returned nonzero image ID with `quantapdf_composer_draw_image()`.
The fit policy can contain, cover (with clipping), or stretch the image.
Opaque 8-bit RGB PNG and 8-bit RGBA PNG are supported; PNG alpha is preserved
as a PDF soft mask. JPEG supports baseline 8-bit gray and RGB images;
progressive JPEG is rejected before decode to preserve the configured memory
bound.

2.17 also accepts already-decoded caller rasters through
`quantapdf_composer_add_raster()`. V1 supports Gray8, RGB24, and straight-alpha
RGBA32 with explicit dimensions, source stride, and source byte size. Visible
rows are copied into tightly packed Composer-owned storage, so source padding is
ignored and the caller may modify or free its buffer immediately after a
successful call. Gray8 lowers to DeviceGray, RGB24 to DeviceRGB, and RGBA32 to
DeviceRGB plus an 8-bit soft mask. Only canonical visible bytes are charged to
the Composer resource budget; the returned image ID is placed with the existing
`quantapdf_composer_draw_image()` API.

2.18 adds reusable Composer graphics-state resources for opacity and blend
modes. `quantapdf_composer_add_graphics_state()` registers immutable fill
alpha, stroke alpha, and a typed blend mode (Normal, Multiply, Screen, Overlay,
Darken, or Lighten). Existing text, embedded-text, glyph-run, image, and path
option records expose the state ID only through an additive V2 tail; V1 sizes
remain the legacy layouts and therefore retain opaque Normal rendering and
byte-compatible output. The backend lowers referenced states to PDF ExtGState
resources and scopes each `gs` application with balanced `q/Q` so state does
not leak between operations.

2.19 adds reusable Composer paint resources for linear and radial gradients.
`quantapdf_composer_add_linear_gradient()` and
`quantapdf_composer_add_radial_gradient()` copy 2–64 strictly ordered opaque
sRGB stops and return a generic `quantapdf_composer_paint_id`. Path options
attach fill/stroke paint IDs only through an additive V3 tail; paint ID 0 keeps
the existing solid ARGB contract. Linear/radial geometry is expressed in local
displayed-page coordinates, with an optional finite non-singular affine
transform. V1 gradient spread is PAD only. The qpdf backend lowers paints to
axial/radial shadings and Pattern resources; no second vector backend is
introduced.

2.20 adds immutable clip-path resources. `quantapdf_composer_add_clip_path()`
copies generic path commands plus nonzero/even-odd clip rule and an optional
displayed-space affine transform. Graphics-state options attach the clip only
through an additive V2 tail, so text, embedded text, glyph runs, images, and
paths all reuse the existing `graphics_state_id`. Clip geometry is numerically
mapped into page PDF coordinates and emitted as `W`/`W*` + `n` inside the
operation's balanced `q/Q` scope; the clip transform never changes the
subsequent operation coordinate system.

2.21 adds reusable Composer Form XObjects without a parallel drawing API.
`quantapdf_composer_add_form()` invokes a synchronous builder callback with a
temporary transparent one-page child Composer; the callback uses the existing
text/path/image/paint/graphics-state/clip/form APIs. Successful content is
deterministically serialized, deduplicated by exact snapshot bytes, and stored
as an immutable parent resource. `quantapdf_composer_draw_form()` places the
resource with a displayed-space affine transform and optional existing
graphics-state ID. The qpdf backend converts the stored child page through
`getFormXObjectForPage()`, preserving isolated fonts, images, patterns,
ExtGState resources, nested forms, and text extraction metadata.

2.22 extends the bounded static SVG lowering path without adding an SVG-specific
backend. V2A adds numeric stroke dash arrays/offsets, inherited fill/stroke
opacity plus leaf opacity, elliptical A/a path arcs normalized to cubic Béziers,
and root preserveAspectRatio mapping including meet, none, and slice. Slice uses
a transactional Composer clip resource for the destination viewport. Group
opacity other than 1 remains fail-closed because correct group compositing is
not equivalent to multiplying child alpha. Reference-bearing gradients,
clipPath, and reusable definitions are handled separately by the SVG V2B
definition-graph workstream.

2.23 adds the first safe local SVG definition graph. A side-effect-free pre-pass
collects unique IDs plus `linearGradient`, `radialGradient`, and `clipPath`
definitions under root `defs`; the render pass then resolves forward/backward
fragment-local `url(#id)` references. V1 paint servers require
`gradientUnits="userSpaceOnUse"`, PAD spread, opaque stops, and no template
href. Clip paths require absent/`userSpaceOnUse` units and lower through the
Composer clip primitive. External URLs, wrong-kind/unresolved refs, duplicate
IDs, objectBoundingBox semantics, nested clip refs, and group/root clip-path
remain fail-closed. Gradient/clip/state resources are registered only during
transactional publish and are rolled back on failure. Reusable `symbol/use`
definitions remain the separate SVG V2B2 workstream.

The original `quantapdf_composer_draw_text()` contract remains Base-14 +
WinAnsi and is unchanged. For embedded fonts, register caller-owned SFNT bytes
with `quantapdf_composer_add_font()`, then draw UTF-8 through
`quantapdf_composer_draw_embedded_text()` using the returned nonzero
`font_id`. QuantaPDF copies and owns the font bytes; registering identical
bytes again reuses the existing ID.

The embedded-font path accepts both TrueType and OpenType CFF-family
outlines. TrueType (`glyf`/`loca`) fonts continue to use deterministic
subsetting, `FontFile2`, and `CIDFontType2`. OpenType fonts carrying
`CFF ` or `CFF2` use the shared SFNT metrics/cmap parser and are embedded as
complete `FontFile3 /Subtype /OpenType` programs under `CIDFontType0`.
Generated PDFs are raised to PDF 1.6 only when such a CFF resource is actually
used. Unicode cmap format 4 and 12 remain supported and `ToUnicode` is emitted
for extraction.

`quantapdf_composer_draw_embedded_text()` remains the simple direct-cmap path:
it maps Unicode code points to glyph IDs and does not claim GSUB/GPOS shaping,
bidi reordering, ligature formation, or script-specific shaping. Missing glyphs
fail explicitly.

2.14 adds deterministic preflight layout queries for both text paths.
`quantapdf_composer_measure_text()` and
`quantapdf_composer_measure_embedded_text()` accept the same text options plus
a positive maximum layout width and return tight laid-out width, baseline-model
height, and line count without adding a Composer operation. Measurement and
drawing share one private layout implementation, so wrapping, tabs, trailing
whitespace, Base-14 metrics, embedded-font advances, invalid UTF-8, and missing
glyphs cannot drift between two algorithms. Horizontal alignment does not
change the measured extent.

2.15 adds explicit affine placement for Base-14 text, embedded direct-cmap
text, and caller-shaped glyph runs. `quantapdf_affine_transform` uses the
same displayed page space as the rest of the public API:

```text
X = a*x + c*y + e
Y = b*x + d*y + f
```

The transformed entry points keep their text bounds and glyph-run origins in
local displayed coordinates. Layout, wrapping, alignment, and vertical
line-admission happen first; the affine transform is then applied to each
baseline/glyph placement. Existing draw APIs are unchanged and are exactly the
identity-transform form of the new implementation. Rotation, scale, reflection,
and skew require no qpdf types or shaping dependency in the public ABI.

2.16 adds copied dash patterns for generic Composer paths.
`quantapdf_composer_draw_path_dashed()` reuses the existing
`quantapdf_composer_path_options` paint contract and accepts a separate
`quantapdf_composer_dash_pattern`. Dash lengths are finite and nonnegative,
with at least one positive element; zero-length elements are permitted, phase
is finite and nonnegative, and the count is bounded to 64. The Composer copies
the caller array and charges command plus dash storage atomically to the
resource budget. Existing `quantapdf_composer_draw_path()` remains solid and
unchanged.

For text shaped by another engine, 2.11 adds
`quantapdf_composer_draw_glyph_run()`. Each fixed-layout glyph record carries
a font glyph ID, x/y advance and offset in 1000/em units, and an optional byte
range into a caller-provided UTF-8 cluster buffer. The run origin is an explicit
displayed-page-space baseline point. A glyph may map to multiple Unicode
scalars for ligatures/alternates, while a secondary combining glyph may use
`unicode_length = 0` to avoid duplicate extraction. For TrueType glyph runs, QuantaPDF assigns PDF CIDs independently of glyph IDs
and emits a private `CIDToGIDMap` plus `ToUnicode`. PDF only defines
`CIDToGIDMap` for Type2 CIDFonts, so CFF/CFF2 runs instead require
`CID == GID`. They support positioned glyphs and cluster extraction as long as
one font GID is not required to represent multiple distinct Unicode clusters in
the same Composer; that ambiguous case fails explicitly as unsupported.
QuantaPDF still does not perform shaping itself; HarfBuzz/FreeType/etc. remain
optional caller-side producers of glyph runs.


2.13 adds `quantapdf_composer_draw_svg()` as a convenience ingestion layer
over the existing generic path IR. The caller supplies bounded SVG bytes plus a
destination rectangle; QuantaPDF performs no file or network access. V1
requires a `viewBox` and supports `svg`, `g`, `path`, `rect`, `line`,
`polyline`, `polygon`, `circle`, and `ellipse`. Path commands
M/L/H/V/C/S/Q/T/Z are supported; quadratic segments are normalized to cubic
Béziers before publication. Fill/stroke inheritance, fill rule, stroke
width/cap/join/miter, inline presentation style, and nested
matrix/translate/scale/rotate/skew transforms are supported within the
documented subset.

SVG ingestion is deliberately non-browser-like and fail-closed. It rejects
DOCTYPE/entity processing, scripts, text, `use`, images, external references,
arc commands, unsupported CSS/presentation properties, and malformed markup.
All geometry is parsed and staged before any Composer operation is published.
Because the current path IR stores a scalar stroke width rather than a full
graphics-state transform, arbitrary affine transforms are exact for fill-only
geometry while stroked paths require a conformal transform (translation,
rotation/reflection, and uniform scale). Non-uniform/skewed stroke transforms
return `QUANTAPDF_ERROR_UNSUPPORTED` rather than approximating the stroke.

The default capacity is 1,024 pages, 1,000,000 draw operations, and 256 MiB for
owned text/image resources and bounded image-decoder working memory. Supply
`quantapdf_composer_options` to lower or raise those limits. Crossing a
configured limit returns
`QUANTAPDF_ERROR_UNSUPPORTED` without partially publishing the operation.

## Rendering

The convenience call:

```c
quantapdf_render_page(page, &bitmap);
```

renders the full page at 72 DPI, zero additional rotation, opaque DeviceRGB, and a white background.

For explicit rendering use a versioned options struct:

```c
quantapdf_render_options options = {
    sizeof(quantapdf_render_options),
    144.0f, /* dpi */
    0.0f,   /* rotation_degrees */
    0,      /* clip_enabled */
    { 0 },  /* clip in displayed page space */
    0       /* alpha */
};

quantapdf_render_page_with_options(page, &options, &bitmap);
```

`struct_size` is part of the ABI contract. Callers should initialize it to the size of the struct they compiled against. Render options are a single append-only structure; the library ignores fields beyond the caller-provided size so older binaries retain their original defaults.

`dpi` is the canonical zoom/resolution input: 72 DPI is scale 1.0, 144 DPI is scale 2.0. Rotation is in degrees and is applied only to that render call. When clipping is enabled, `clip` is expressed in displayed page space and is transformed by the same DPI/rotation matrix; QuantaPDF renders directly into the clipped device bbox rather than allocating a full-page intermediate image.

`alpha` accepts only 0 or 1:

- `0`: 8-bit interleaved RGB, opaque white untouched pixels;
- `1`: 8-bit interleaved RGBA, transparent untouched pixels.

RGBA samples produced by the renderer use **premultiplied alpha**. `stride` returned by `quantapdf_bitmap_dimensions` is the authoritative byte distance between rows; callers must not assume a different packing rule. The pointer returned by `quantapdf_bitmap_data` is borrowed read-only storage and remains valid only until `quantapdf_drop_bitmap`.

## Thumbnails

```c
quantapdf_render_thumbnail(page, max_width, max_height, &bitmap);
```

renders opaque RGB while preserving the page aspect ratio. The result fits inside the requested pixel box and never upscales beyond the page's 72-DPI size. Thumbnail rendering derives a DPI and reuses the same renderer rather than maintaining a separate raster path.

## Document audit and sanitization

Initialize the size-tagged audit result before calling the audit API, then test
the independent finding bits that matter to your policy:

```c
quantapdf_audit_result audit = {
    .struct_size = sizeof(quantapdf_audit_result),
    .findings = 0
};

if (quantapdf_document_audit(doc, &audit) == QUANTAPDF_OK) {
    if (audit.findings & QUANTAPDF_AUDIT_JAVASCRIPT_ACTION) {
        /* JavaScript action or JavaScript name tree is reachable. */
    }
    if (audit.findings & QUANTAPDF_AUDIT_SIGNATURE) {
        /* A signature structure is present; validity is not checked. */
    }
    if (audit.findings & QUANTAPDF_AUDIT_ENCRYPTION) {
        /* The input is encrypted, even if it was opened with a password. */
    }
}
```

Sanitization returns a new immutable output and never mutates the opened
document:

```c
quantapdf_output *output = NULL;

if (quantapdf_sanitize(doc, QUANTAPDF_SANITIZE_ALL, &output) ==
    QUANTAPDF_OK) {
    const unsigned char *data = NULL;
    size_t size = 0;

    quantapdf_output_data(output, &data, &size);
    /* data[0..size) is borrowed read-only storage owned by output. */

    quantapdf_close(doc);
    doc = NULL;
    /* data remains valid because output owns its bytes independently. */

    quantapdf_drop_output(output);
}
```

`QUANTAPDF_SANITIZE_ALL` removes the seven sanitizable active-content classes.
Safe internal `/GoTo` navigation is not reported as dangerous and is not
removed. Signed or encrypted inputs are intentionally rejected by
`quantapdf_sanitize()` with `QUANTAPDF_ERROR_UNSUPPORTED` because rewriting
would invalidate a signature or silently decrypt the document.

The audit is a strict, catalog-reachable structural inspection of conventional
PDF security and active-content mechanisms. It is not antivirus or malware
detection, content keyword scanning, cryptographic signature validation, or
proof that arbitrary custom object edges are safe. Malformed conventional
structures return `QUANTAPDF_ERROR_FORMAT`; a bounded traversal or a requested
removal that cannot be proven complete returns `QUANTAPDF_ERROR_UNSUPPORTED`
without publishing output.

## PDF security rewrite

Security changes are explicit immutable transforms. V1 writes only the PDF
Standard Security Handler revision 6 with AES-256; decrypt and re-encrypt
accept encrypted documents that `quantapdf_open()` has already authenticated.
Signed inputs are rejected because a full rewrite would invalidate signatures.

```c
quantapdf_encryption_options security = {
    .struct_size = QUANTAPDF_ENCRYPTION_OPTIONS_V1_SIZE,
    .method = QUANTAPDF_ENCRYPTION_AES_256,
    .user_password_utf8 = "reader",
    .owner_password_utf8 = "document-owner",
    .permissions = QUANTAPDF_PERMISSION_COPY |
        QUANTAPDF_PERMISSION_PRINT_LOW_RESOLUTION,
    .encrypt_metadata = 1
};
quantapdf_output *encrypted = NULL;

if (quantapdf_encrypt_pdf(doc, &security, &encrypted) == QUANTAPDF_OK) {
    quantapdf_output_save_file(encrypted, "encrypted.pdf");
    quantapdf_drop_output(encrypted);
}
```

V1 passwords are NUL-terminated preparation-invariant printable ASCII
(`0x20..0x7e`) with a maximum of 127 bytes. The user password may be empty;
the owner password must be nonempty and distinct. No password is truncated.
The field names retain the `_utf8` suffix because printable ASCII is a strict
UTF-8 subset; arbitrary Unicode passwords are intentionally deferred until a
standards-compliant preparation dependency is available.

Permissions are advisory viewer hints, not DRM. High-quality printing requires
the low-resolution print bit. `ANNOTATE_AND_FILL_FORMS` reflects PDF bit 6,
which permits both annotation changes and filling existing fields;
`FILL_FORMS` controls the independent PDF bit 9. Accessibility extraction is
always available for R6 and therefore has no misleading public toggle.

Encryption and re-encryption intentionally produce different bytes on repeated
calls. QuantaPDF supplies qpdf with OS-backed cryptographic randomness and
generates fresh security material. Decryption is deterministic for identical
authenticated input. Every result owns its bytes independently of the source:

```c
quantapdf_document *authenticated = NULL;
quantapdf_output *plaintext = NULL;

if (quantapdf_open("encrypted.pdf", "reader", &authenticated) ==
        QUANTAPDF_OK &&
    quantapdf_decrypt_pdf(authenticated, &plaintext) == QUANTAPDF_OK) {
    quantapdf_output_save_file(plaintext, "plaintext.pdf");
}
quantapdf_drop_output(plaintext);
quantapdf_close(authenticated);
```

To edit an encrypted document, decrypt it, save and reopen the plaintext,
perform the desired explicit transforms, then encrypt the final document.
Existing save, crop, trim, flatten, recompress, rewrite, edit, and Composer
operations never add, remove, or replace encryption implicitly.

Shared builds isolate the private qpdf provider. In a static-link process,
replacing qpdf's process-global provider after QuantaPDF initializes causes
encrypt and re-encrypt to fail `QUANTAPDF_ERROR_UNSUPPORTED`; QuantaPDF never
temporarily overrides or restores the consumer's pointer.

## Lossless rewrite and garbage collection

```c
quantapdf_output *rewritten = NULL;

if (quantapdf_rewrite_lossless(doc, &rewritten) == QUANTAPDF_OK) {
    quantapdf_output_save_file(rewritten, "canonical.pdf");
    quantapdf_drop_output(rewritten);
}
```

`quantapdf_rewrite_lossless()` performs a deterministic full rewrite with a
fixed policy. It removes indirect objects unreachable from the trailer graph,
rebuilds the cross-reference state, preserves existing stream encodings, and
does not deduplicate distinct reachable objects. Repeated calls on identical
input—and rewriting a reopened result—produce byte-identical output.

This API is deliberately strict. It does not repair damaged PDFs, recompress
images, or flatten interactive content. It neither preserves nor creates
encryption: encrypted input is rejected with `QUANTAPDF_ERROR_UNSUPPORTED`.
Inputs requiring structural recovery return `QUANTAPDF_ERROR_FORMAT`, and
already signed inputs return `QUANTAPDF_ERROR_UNSUPPORTED`. The returned
output owns its bytes independently of the source document.

## Image recompression

Image recompression is an explicit lossy transform that returns a new owning
output and leaves the opened document unchanged:

```c
quantapdf_image_recompression_options options = {
    .struct_size = sizeof(quantapdf_image_recompression_options),
    .jpeg_quality = 90,
    .max_decoded_bytes_per_image =
        QUANTAPDF_IMAGE_RECOMPRESSION_DEFAULT_MAX_DECODED_BYTES
};
quantapdf_output *recompressed = NULL;

if (quantapdf_recompress_images(doc, &options, &recompressed) ==
    QUANTAPDF_OK) {
    quantapdf_output_save_file(recompressed, "recompressed.pdf");
    quantapdf_close(doc);
    doc = NULL;
    /* recompressed remains valid after its source document is closed. */
    quantapdf_drop_output(recompressed);
}
```

V1 rewrites only reachable, opaque, indirect 8-bit `DeviceGray` and
`DeviceRGB` Image XObjects whose decoded byte count fits the caller's cap.
The default cap is 64 MiB per image. Shared images keep one object identity
and are encoded once; nested Form resources and annotation/Widget appearance
resources are included. Valid unsupported images—including CMYK, ICCBased,
Indexed, masked, non-8-bit, and non-identity-Decode images—are preserved.

`jpeg_quality` accepts 1 through 100. Quality 100 still performs JPEG
encoding and is not a no-op. This API does not resize or downsample images,
rewrite inline images, flatten alpha, convert color spaces, preserve digital
signatures, or retain encryption. Signed and encrypted inputs fail with
`QUANTAPDF_ERROR_UNSUPPORTED`; malformed consumed image/resource structures
fail closed without publishing output.

## Flatten interactive content

```c
quantapdf_output *flattened = NULL;

if (quantapdf_flatten_interactive(
        doc,
        QUANTAPDF_FLATTEN_ANNOTATIONS | QUANTAPDF_FLATTEN_WIDGETS,
        &flattened) == QUANTAPDF_OK) {
    quantapdf_output_save_file(flattened, "flattened.pdf");
    quantapdf_drop_output(flattened);
}
```

`quantapdf_flatten_interactive()` bakes existing normal appearance streams
into page content. Annotation and Widget selection are independent flags, and
the source document remains immutable. Widget flattening also prunes the
affected AcroForm field tree and calculation order.

The transform never synthesizes appearances, applies redactions, executes
form actions, or flattens Links. It fails closed for missing or malformed
appearances, malformed or unbalanced changed-page content, non-unit annotation
opacity, contradictory Widget page ownership, unsupported interactive
semantics, visible Links on changed pages, tagged PDFs that require structure
updates, encryption, and signatures.
Output is deterministic and idempotent.

## Dependency model

The backend foundation has two pinned dependency paths:

- `cmake/QuantaPDFPdfium.cmake` downloads the exact platform PDFium artifact,
  verifies its SHA-256, version, disabled V8/XFA settings, and license payload,
  and rejects unsupported platforms.
- `vcpkg.json` resolves qpdf `12.4.0` from the pinned registry baseline with
  optional OpenSSL, GnuTLS, and Zopfli features disabled.
- PDFium and qpdf remain private implementation dependencies; only the
  `quantapdf_*` C ABI is public.

The target architecture is:

```text
PDFium public C API ----+
                       +--> QuantaPDF private adapters --> quantapdf_* C ABI
qpdf C++ object graph --+
```

Only the `quantapdf_*` ABI is exported by the wrapper.

## Build with CMake Presets and vcpkg

Set `PROJECT_ROOT` to the parent package workspace and `VCPKG_ROOT` to a
vcpkg checkout containing the baseline in `vcpkg.json`. The configure presets
run manifest installation automatically. Public
configure, build, and test entry points live in the versioned
`CMakeUserPresets.json`.

### Linux x64

```sh
export PROJECT_ROOT=/opt
export VCPKG_ROOT=/opt/vcpkg

cmake --fresh --preset linux-release-user
cmake --build --preset linux-release-user
ctest --preset linux-release-user
```

### macOS

Use `macos-arm64-release-user` on Apple Silicon or
`macos-x64-release-user` on Intel, then run the same configure, build, and
CTest preset sequence.

### Windows x64 DLL

Run from an x64 Visual Studio Developer Command Prompt so `cl`, Ninja, and
the MSVC runtime are available:

```bat
set PROJECT_ROOT=C:\projects\cpp
set VCPKG_ROOT=C:\tools\vcpkg

cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user
```

Use `win-dev-user` for the isolated Debug + AddressSanitizer profile.

The test build stages `quantapdf.dll` beside every Windows test executable. Consumers are responsible for deploying `quantapdf.dll` beside their executable or otherwise making it available through normal Windows DLL search rules.

## Tests

CTest covers the document lifecycle plus Page + Render contracts, including invalid arguments, page geometry, MediaBox/CropBox coordinates, RGB/RGBA output, versioned render options, DPI, rotation, clipping, thumbnails, encrypted PDFs, malformed input, repeated lifecycle stress, interleaved handles, UTF-8 paths, deterministic lossless rewrite/GC, and strict interactive-content flattening.

Linux additionally runs AddressSanitizer and UndefinedBehaviorSanitizer.

## CI

Normal pull-request updates use Linux as the fast development loop. Windows and macOS are reserved for explicit `full-ci` checkpoints, manual workflow dispatch, and pushes to `master`.

The workflow persists vcpkg binary packages and the exact hash-verified PDFium
archive. Cache identities include OS/architecture, the pinned vcpkg commit,
manifest content, and literal PDFium release `chromium-8021`.

A feature is not considered cross-platform complete until Linux, macOS, and Windows pass on the same exact head SHA. Older green runs do not satisfy acceptance for a newer head.

## License

QuantaPDF source is distributed under the **Apache License 2.0**. See
`LICENSE` and `THIRD_PARTY.md`.

The PDFium and qpdf backend dependencies use permissive licenses. Their pinned
license payloads and redistribution notices are installed with QuantaPDF; see
`THIRD_PARTY.md` for details.
