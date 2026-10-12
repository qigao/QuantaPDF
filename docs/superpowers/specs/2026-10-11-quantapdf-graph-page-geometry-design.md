# Selected Graph page geometry and canonical path lowering

Continuation of [#145](https://github.com/qigao/QuantaPDF/issues/145) and
[#154](https://github.com/qigao/QuantaPDF/issues/154), stacked after
Draft #201 → #200 → #199 → #198.

## Evidence and scope

The previous restricted PDF Path → Composer IR converter accepts caller-supplied
width and height, and only knows zero-origin, no-rotation, unit-scale page
coordinates. Calling it with incorrect dimensions would invert the Y axis
wrongly and produce a visually different PDF. This increment introduces a
provider-independent private geometry admission gate based **only on the
selected sealed Graph Snapshot**, before any decoded stream is requested.

As defined by ISO 32000 page attributes, /MediaBox (required), /CropBox
(optional, default MediaBox) and /Rotate (optional, default zero) are inheritable;
the nearest present attribute on the /Page→/Pages ancestry wins.
/UserUnit belongs to the leaf /Page, has positive numeric value and defaults
to 1; no ancestor inheritance is permitted. Effective rotation is normalized
modulo 360, after requiring an integral multiple of 90 degrees. Numeric
values are checked without process-locale-sensitive parsing.

## Validated ancestry

The indexed page must have indirect /Type /Page and reach the Catalog's exact
indirect /Pages root through /Parent links. Each intermediate node must be an
indirect /Type /Pages dictionary whose /Kids array contains the immediate
child exactly once. The root must not itself have /Parent. The call accepts
an explicit nonzero max_parent_steps (leaf and root included), also bounded
by node_count. A real cycle, wrong parent, missing required MediaBox, wrong
types or invalid rectangles is FORMAT; exhausting a caller depth budget
before reaching the root is UNSUPPORTED.

Read-only lookup uses the selected immutable model; a commit replacing a
MediaBox number is observed by that descendant and leaves the base unchanged.
The returned geometry contains detached doubles, no owned or borrowed graph
storage. All output slots reset on failure.

## Canonical admission and composition

`quantapdf_graph_canonical_page_dimensions` accepts only:

- Effective MediaBox = CropBox = `[0 0 width height]` with positive dimensions.
- Effective rotation 0 (equivalent multiples of 360 are accepted).
- Page /UserUnit absent or numerically equal to 1.

Every other valid case (offset/unequal CropBox, page rotation, nonunit
UserUnit) returns UNSUPPORTED; malformed data is FORMAT. The graph-to-Composer
facade first performs ancestry, attribute, and canonical checks, **before**
running the existing bounded whole-page /Contents decoder, graphics evaluator
and strict path lowering. Width/height are derived, never supplied by the
caller. Float/decimal and RGB8 admission in #201 is not weakened.

The facade produces the same detached `quantapdf_ir` ownership and disposal
contract as #201. Failure never publishes partial output. The public C11 ABI,
installed symbols, provider dependencies, writer behavior and released API
remain unchanged. No attempt to apply initial page CTMs, CropBox clips or
rotation to unsupported pages is made.

## Verification

A focused QPDF graph test builds a nested /Pages tree and tests inherited
MediaBox/default CropBox/default rotation, local overrides, normalized rotation,
non-inheritable UserUnit, malformed boxes, invalid rotation/units, contradictory
/Parent and /Kids links, root with parent, caller depth exhaustion and zeroed
failure outputs. A graph commit test replaces the MediaBox's third numeric value
and proves selected-version isolation after dropping the base.

An end-to-end private Snapshot test uses an in-memory QPDF-created PDF,
closes the source document, lowers through the new graph-derived geometry
facade, commits a width change, verifies base/descendant dimensions, drops
both snapshots, exports the detached Composer IR via QPDF, and renders with
the existing public PDFium path to check page dimensions and a filled pixel.
Complete Linux Release, fatal ASan/UBSan, SDK installation and Poppler
qualification are required; Windows/macOS remain separate gates.

## Follow-up

Support noncanonical page geometry with explicit, independently tested
affine transforms (page boxes, /Rotate and /UserUnit), then geometrically
equivalent clipping resources. Do not silently flatten valid-but-unsupported
PDF pages into a misleading Composer IR. Arbitrary PDF page import and
full renderer equivalence remain open work under #145/#154.
