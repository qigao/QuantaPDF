# #269 — Original nonzero W Clip left edge, separate private C11 A/B

**Draft / DO NOT MERGE**, directly stacked on independent read-only
post-Fill-right source residual audit #293 (HEAD
`f28fa3b9216532b7f98a88dec4f134099b91f41d`).
Its predecessor #292 completed full CI SUCCESS 3/3, source page
unchanged. This branch must independently pass exact-head CI.

## True source domain

The published 3,344-byte immutable original jsPDF:
SHA256 `179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e`,
596x842 raw original PDFium pixels. PDF source first Strokes a
rectangular path, then defines a nonzero W Clip rectangle with source
display x=50..250, y=20..140. The subsequent solid red Fill
rectangle covers display x=0..150, y=0..100. The visible red
intersection is therefore x=50..150,y=20..100.

The predecessor #292 corrected only the Fill rectangle's RIGHT edge at
x=150. It still measures 4,019 summed RGB absolute error and 516
changed pixels. Strict #293 true-original region attribution measured
**1,659 RGB error over 79 pixels at the LEFT x=50 W Clip +
black Stroke overlap**. At (50,40), current mode6 native C11
is pure red RGB (255,0,0), with underlying black Stroke-alpha 143 and
red Fill-alpha 255. Raw original PDFium RGB is (246,6,6). A
single-source analytic W Clip boundary coverage of ~240/255 allows the
underlying black Stroke to contribute, unlike the old 4x4 opaque
binary sample. This is a falsifiable geometric hypothesis, NOT a
claim about PDFium internals.

## Strict, independent PRIVATE hypothesis

A seventh explicit private C11 symbol
`quantapdf_ir_raster_rect_fill_left_clip_preview_v0` builds on the
previous admitted independent right-Fill alpha candidate and applies
an additional **actual W Clip left-border partial pixel** correction,
using `round(255 * source_rectangle_intersection_width /
device_pixel_width)`. The column comes entirely from the borrowed
owned IR W Clip path plus the exact NativeDeviceGrid geometry; the
implementation has **no literal x=50 or jsPDF-specific coordinate**.

Admission is deliberately narrow: exactly one opaque normal solid
Fill and one nonzero rectangular W Clip, each possibly with genuine
upper-edge retraces. Reject nonrectangular paths, nonunit transforms,
soft masks/blends, raw malformed source, zero-output/budget failures,
or simultaneous left/right boundary pixels. Require the entire
device row to lie strictly inside the effective rectangle in Y:
no corner, top repeated Stroke, or bottom pixel phase changes. All
source color bytes and existing mode6 right-edge alpha remain
identical. Separately pre-admit the additional second analytical
boundary pass: `fill_edges + clip_edges + 2`. When the borrowed
baseline pixel lacks valid red color/nonzero alpha, refuse and
transactionally dispose the entire output, without retry/fallback.
Only a single standard owned RGBA layer exists.

The old `quantapdf_ir_raster_rect_fill_right_preview_v0` remains
independently callable and byte-for-byte verified through the original
whole-page FNV64 `4d2bfdd4bbf32298`. V0 and Stroke modes1–5 are
untouched, no SDK/ABI symbol is exported.

## Gates / no parity promotion

Strict C11 synthesis checks fractional geometric coverage, old
right-only output identity, repeated source top edges, deterministic
repeat and strict zero-output refusals (including the incremental
sample-work budget, invalid W rule, blend and malformed path).

The **separate original jsPDF** A/B CTest (not the near-40s historical
whole-original negative) renders the new Clip-left Fill twice and
composes each with the SAME previously qualified mode5 Stroke in real
source order. It compares all 501,832 original PDFium pixels,
1,505,496 RGB channels, computes mean/absolute/channel gross/FNV64,
requires exact image repeat and restricts all changes to original
x=50,y=21..99 **only in TEST assertions**. The machine checker
freezes mode6 source image and all #293 region totals. **Do not
require an improved mode7 score; a regression is a valid test result.**

Keep #269 Open, #293 and this branch Draft / DO NOT MERGE,
native-original full-page raster unsupported/zero output, formal W/W*
positive count unchanged, public SDK remains PDFium, and #208 cutover
is blocked.
