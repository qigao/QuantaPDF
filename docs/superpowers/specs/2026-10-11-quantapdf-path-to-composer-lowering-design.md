# Safe PDF path semantics -> Composer IR lowering (restricted increment)

Tracks [#145](https://github.com/qigao/QuantaPDF/issues/145), stacked on
draft #200 (#199 -> #198), with no public C ABI changes.

## Verified mapping, not a general PDF importer

The existing Composer path writer serializes all path coordinates via a
top-left-to-PDF Y flip, and prepends the Composer path transform as a PDF CTM.
Path options contain `float` coordinates and CTM elements, RGB8 colors,
stroke width and fill rule. The writer prints most numbers with four decimal
places and supports M/L/C/h commands, with automatic fill closure.

Provide one private, transactional function:
`quantapdf_pdf_path_lower_to_composer(page_semantics, width, height, limits, out_ir)`.

The caller **must supply verified canonical zero-origin page geometry**: no
inherited box offset, page rotation, /UserUnit scaling, crop offsets or page
initial-CTM special cases. This boundary does not infer geometry from the
document graph and does not claim direct whole-document import. The output is
a standalone, uniquely owned single-page `quantapdf_ir`. The source semantics,
graph, provider and stream may die immediately; the caller disposes with
`quantapdf_ir_dispose`.

The conversion applies the inverse of Composer's PDF emission transform.
For a source row-vector PDF CTM `[a b c d e f]` and page height H, use
Composer's top-down `[a -b -c d (e + c H) (H (1-d) - f)]`. Its writer then
recreates the source CTM while also reversing path Y. Path coordinates are
converted to `(x, H-y)`. For `v` the first cubic control point is the current
point; for `y` the second is the end point. `re` expands to move + 3 lines
+ close. `s/b/b*` add an explicit close command.

Paint/graphics state are copied per painted path from #200's typed records:
stroke/fill colors, width, the composed CTM, fill rule and default cap/join/miter.
The emitted page suppresses an artificial opaque background.

## Fail-closed admission

- Every source numeric value must fit both the writer's float storage and
  four-decimal serialization grid. Page geometry and generated CTM entries
  undergo roundtrip checks; singular or nonfinite CTMs are unsupported.
- Only RGB8 colors whose emitted four-decimal values equal the source input
  are admitted. For example 0.2/0.4/0.6 are admitted; 0.5 is rejected
  rather than silently quantized to 128/255.
- The entire input is rejected if it contains any clip chain, W/W*, n-only
  path, text/image/resource operator, degenerate path without segments,
  or unknown paint. This phase does not create fake clipping resources.
- Explicit max painted paths and max commands cap output allocations.
  Inputs with unrepresentable numbers and truncated/rejected paths publish
  no operation/page records at all.
- A successfully produced Composer IR owns its own command arrays and page;
  no external resources or context are retained.

This is a narrow semantic projection into the **existing** Writer vocabulary,
not a promise of matching all PDF painting corner cases or raster output for
general PDFs.

## Validation

Add a native C11 test linking the actual Composer writer, with strict path
options and typed-operator/CTM checks, cubic v/y expansion, rectangles,
fill/close/dual paint, RGB quantization rejection, zero-segment and clipping
rejection, resource budgets, output reset, deterministic double export,
disposal, and a public PDFium pixel test for a translated colored rectangle.

The existing #200 Linux CTest, fatal ASan/UBSan, static SDK installation,
Poppler qualification and remaining Windows/macOS qualification are merge
gates. No change is made to already released Composer APIs or Writer policy.

## Next milestones

A Graph-to-canonical-page-geometry admission gate is required before a
document-level automatic PDF-to-Composer-import facade can be safe.
Geometrically identical clipping resource mapping, richer colors, line caps,
joins, dash patterns, fonts/images and unknown operators remain explicit
future work under #145 and #154.
