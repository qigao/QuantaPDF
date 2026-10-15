# #269 — Private bounded page-to-device grid (phase 1)

Stacked strictly on #328, Draft / DO NOT MERGE. This increment is deliberately
**not linked into any production target or renderer**. It does not relax the
existing geometry-refusal matrix or change the original jsPDF sentinel.

## Data flow and ownership

The sealed selected Graph returns detached effective MediaBox/CropBox/Rotate/
UserUnit metadata. Existing `quantapdf_graph_page_display_map()` converts
source PDF Y-up to zero-origin visible PDF Y-up. New private
`quantapdf_graph_page_device_grid()` composes the Y-down pixel-edge affine:
`X = s * (a*x + c*y + e)`;
`Y = s * (display.height_points - (b*x + d*y + f))`,
where `s=dpi/72`. This maps **pixel edges**; sampling for destination pixel
`(i,j)` happens at `(i+1/2,j+1/2)`. The output contains no source pointers.

Output width/height are `ceil(visible_dimension_points * dpi / 72)` with no
floor/truncation of the source MediaBox or crop origin. Source coordinates
and owned IR path-command bytes are unchanged. Rotation is the already
validated intrinsic PDF right-angle rotation (0/90/180/270). Requested render
rotation is not implemented in this phase and must stay rejected at its
existing calling boundary.

## Admission

The v0 grid permits exactly DPI=72/96/144, RGB8 and caller-supplied nonzero
pixel/byte caps. The helper checks finite numbers, UINT32 width/height,
size_t multiplication, max pixels and max bytes **before publishing output**.
All failure paths reset the entire detached output structure. CropBox outside
MediaBox remains unsupported by the pre-existing display mapper. No allocation,
renderer, PDFium dependency or fallback is introduced.

## Required next implementation gates — NOT CLAIMED PASSING

1. Compile the new private .c file in an **isolated C11 test-only target**;
   validate zero MediaBox origin, offset CropBox, 0/90/180/270 rotation and
   UserUnit 0.5/1/2, plus 72/96/144 DPI with asymmetrical corner probes.
2. Test 595.28 x 841.89 at 72 DPI -> 596 x 842; check pixel-edge and
   pixel-center phases, RGB8 stride and source affine. Repeat bitwise.
3. Test invalid/malformed input, NaN/Inf, tiny and huge units, zero caps,
   deliberate pixel cap/byte cap/overflow, unsupported 120 DPI and crop
   outside media. On each refusal assert exact zero fields.
4. Connect **only** the private test renderers, after explicit review of
   transformed stroke widths, clip boundaries, Q8 subpixel phase and
   requested-rotation composition. The exact original jsPDF zero-error
   FNV64 bfba622e41c96ca7 must not change.
5. Use independently published, license-reviewed original PDF files from
   multiple producers with nontrivial CropBox, Rotate and UserUnit.
   Compare independently rendered original PDFium full-page RGB,
   repeat/limit rejection and Linux Release+ASan/UBSan, Windows, macOS.
   Do not count synthetic/re-exported sources as independent positives.

## Release/issue guards

#269 stays OPEN, this and its parent PRs Draft / DO NOT MERGE, formal
original-native positive count stays unchanged, installed/public SDK remains
PDFium, #208 cutover remains blocked. No CI success or original-source
conformance is asserted by the introduction of this isolated helper alone.
