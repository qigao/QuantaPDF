# PDFium removal

The target in #208 is a production SDK with no PDFium runtime, link, download
or installed-package dependency. qpdf remains a private structure/writer
dependency. PDFium may be used by independent test oracles after cutover.

## Document observations increment

The production built-in provider now opens the immutable source with qpdf.
Counting pages, creating/dropping page handles, reading page bounds/boxes,
and extracting links do not initialize or open PDFium. Page counting and
destination lookup traverse the original page tree without qpdf's repairing
`getAllPages()` enumeration: invalid annotation attributes must remain visible
to the observation that validates them.

Links are copied from page annotations. Supported observations include URI,
explicit and named internal destinations, GoTo, XYZ nullable coordinates and
the existing point-only representation of fit destinations. Old `/Dests` and
`/Names/Dests` name trees are supported. Source and target geometry use their
own visible MediaBox/CropBox intersection, inherited quarter rotation and
leaf UserUnit. The C facade applies each UserUnit once. Non-XYZ destinations
retain a zero target point because the public record has no fit/zoom fields.

The projection rejects malformed annotations/destinations, unknown actions,
cyclic name trees, traversal exhaustion, more than 16,384 links or more than
1 MiB of aggregate URI bytes. Rejection publishes no partial snapshot. Name
tree depth is limited to 64, page-tree depth to 256, and traversal work uses
the existing source-size work budget. Copied snapshots outlive page/document
owners. Source bytes and password remain owned by the existing facade.

`quantapdf.native_document` compiles the actual production facade and structure
sources with no PDFium headers, sources or link target. Aborting tripwires
stand in for the remaining legacy operations: invoking one fails the test.
This is a dependency-isolation test, not a second SDK/backend configuration.
It covers all quarter rotations, differing source/target UserUnits, inherited
boxes, URI/GoTo/named links, nullable XYZ, fit destinations, encrypted inputs,
detached lifetimes and atomic malformed/unsupported/budget rejection.
It also exercises metadata, an edit session and lossless rewriting without
loading the legacy reader. Image observations now use the bounded
[native image projection](native-images.md), with detached sample/quad
coverage in the same no-PDFium executable.

## Remaining production dependency

| Surface | Current implementation | Required replacement |
| --- | --- | --- |
| Page rendering and thumbnails | PDFium, loaded only when requested | Native source interpretation, general paint/Stroke/Clip, text/fonts, images, forms, transparency, device transforms and annotations |
| Plain and structured text, search | PDFium text page | Native font encodings/ToUnicode, text state, Unicode and geometry observations |
| Image occurrences and decoded bitmap snapshots | Native bounded placement/Gray/RGB/Indexed/JPEG/masks | Expand remaining inline/stencil/complex-color/mask coverage; no PDFium caller remains for this API |
| Reader lifecycle for render/text | PDFium adapter/runtime | Remove after both callers migrate |
| CMake, installed SDK and license packaging | PDFium remains required | Remove production import, transitive linkage, runtime copying and package targets; isolate oracle setup to tests |

The former PDFium link and image projections are deleted. PDFium page-count/bounds helper
functions remain for the direct legacy-adapter test, not built-in dispatch.
The legacy adapter's source-rewrite compatibility path is still confined to
unmigrated observations and disappears with that adapter; it is not part of
the native interpretation contract.

## Next implementation sequence

1. Expand remaining native image formats and migrate text/font interpretation
   into detached native observations, reusing the selected Graph/resource model.
2. Feed those admitted resources to the native renderer; complete general
   Stroke/Clip, paint, form/transparency and device-option coverage. Keep
   unsupported publication atomic, without a retry or fallback renderer.
3. Route public render/extraction calls to their native implementations, then
   delete the PDFium reader/runtime and all production package dependencies.
4. Verify shared/static installed C11 consumers without PDFium present, all
   platforms/sanitizers, and representative original-source documents. Use
   bounded visual-quality comparisons and semantic tests; do not require
   identical antialiasing to declare usable native rendering.

Each increment needs behavior and ownership evidence. The corpus intake
tasks remain distinct from implementing these production replacements.
