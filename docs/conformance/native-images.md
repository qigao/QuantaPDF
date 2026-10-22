# Native image observations

The public image snapshot API now uses the structure provider and bounded
native decoding. It does not open PDFium. Image occurrence placement is read
from original page/Form contents; samples and quads are copied into the
existing detached snapshot layout. Page/document owners may be destroyed
before image inspection or bitmap creation. No new public ABI is introduced.

## Admitted image observations

- Repeated Image XObjects, inherited page resources, local Form resources,
  nested Form matrices and q/Q placement across multiple content streams.
- Visible page-box intersection, inherited quarter rotation and leaf UserUnit
  produce public display-coordinate quads. Unlike the former image adapter,
  UserUnit is applied explicitly, consistently with page bounds and links.
- DeviceGray/DeviceRGB samples at 1/2/4/8/16 bits, row padding, Decode ranges,
  named color resources and Indexed palettes with Gray/RGB bases.
- Bounded qpdf lossless stream decoding, including predictor processing.
  JPEG uses the existing libjpeg dependency through an owned C decoder:
  original dimensions, component counts and working budget are checked before
  pixel allocation. Baseline Gray/RGB JPEGs and lossless wrappers are admitted.
  PDF DCT ColorTransform 0/1 is explicit; it is not guessed from the PDF color
  space. Progressive JPEG remains unsupported under the existing JPEG policy.
- Intrinsic grayscale soft masks, nearest sample mapping for differing mask
  dimensions, Matte correction, color-key masks and nonstroking alpha from gs.
  The retained bitmap is RGB or premultiplied RGBA; fully opaque masks preserve
  RGB output. q/Q restores placement and opacity independently of resources.

The old PDFium image traversal/decoding entry point and its bitmap retry path
are removed. Rendering and structured text/search remain production PDFium callers;
plain text now uses [native source interpretation](native-text.md).

## Refusal and work bounds

Unknown/inline-image operators, stencil images, explicit mask streams,
CMYK/ICC/calibrated/special color spaces, external graphics-state soft masks
and custom transfer functions are not admitted. These remain native coverage
work under #145/#208. Unsupported cases return an explicit error with no
partial image list; there is no PDFium retry or partial-success inventory.

Each projection bounds decoded content to 8 MiB, retained pixels to 64 MiB,
occurrences to 4,096, operator/lookup visits to 1,000,000, Form depth to 32,
graphics saves to 128 and filter chains to 16. Each new image also needs room
for its worst-case RGBA size before decoding. Source dimensions, row sizes,
sample counts and JPEG dimensions are checked independently. These output
limits are not an aggregate quota for every allocator internal to qpdf or
libjpeg. Parser streams live in a temporary QPDF instance; repeated observation
does not add synthetic streams to the source provider's document.

## Verification

`quantapdf.native_document` compiles the actual production facade and structure
sources without the PDFium adapter/runtime, upstream headers or link target.
Its image scenarios cover independent pixel/quad goldens, transformed Forms,
split content, inherited resources, rotation/UserUnit, all admitted bit depths,
Indexed/Decode, masks, q/Q opacity restoration, Gray/RGB/wrapped JPEG,
ColorTransform 0/1, detached lifetimes, late unsupported content and capacity
refusals. Existing image, bitmap, Composer, crop/trim, recompression and security
semantics tests run against the same public image path.

These are native image-observation qualifications. They do not certify full
source-image rendering or complete PDFium-free SDK packaging.
