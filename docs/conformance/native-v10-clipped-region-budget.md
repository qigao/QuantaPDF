# Native C11: actual clipped-region sample-work bounds

Continuation of [Issue #233](https://github.com/qigao/QuantaPDF/issues/233)
and [Issue #214](https://github.com/qigao/QuantaPDF/issues/214), stacked
on qualified Draft #548.

## Narrow rationale

The original yWorks 600×600 W/W* source remains **UNSUPPORTED** by
current public Source V10 because its bounded private graph has three
paints, four Clip leaves, three intersections and three graphics states.
Even before adding that ownership/scope capability, the canonical
C11 V1 raster precharged every PATH and Clip edge across **all**
600×600×16 device subpixels, including completely invisible regions.
An ordinary 4-edge Fill plus six Clip edges incurred
`(4+6) × 16 × 600 × 600 = 57,600,000` checks, above the unchanged
**8,000,000** limit even if a 40×40 region was all that could paint.

## Exact production change

Only a single already validated, opaque **Fill** operation with at least
one already admitted Clip leaf may use the visible-region work bounds.
This is the existing unique `ir_raster_v1.c` renderer, not a new
raster, fallback, new source parser, or QPDF/PDFium use.

- First fully flatten and validate the original PATH and every Clip
  under their existing edge, coordinate, operation and owner limits.
  Missing, degenerate, malformed and unknown input still fails in the
  old preflight before any pixel allocation.
- Compute a conservative pixel envelope from the endpoints of **all
  actual flattened edges**. Intersect the Fill envelope with each Clip
  envelope, clamped to the selected device canvas. For a closed Fill,
  winding outside its own or any Clip envelope is provably zero. This
  does not infer a rectangle from a bounding box or replace even-odd
  and nonzero winding rules.
- Account the **sum of all Fill and Clip edges** at all 16 subpixels
  of every candidate pixel, including edges that do not intersect a
  particular ray. Check by integer division to avoid budget overflow.
  Only after full admission, skip actual expensive winding checks for
  pixels outside the proven intersection. The same output allocation,
  4×4 V1 winding, RGB alpha blend and lifetime contract remains.
- The numeric max_sample_tests stays **exactly unchanged**. Unclipped
  Fill, Stroke, two PATHs, and every other caller retain the legacy
  whole-page work bill, including prior refusal thresholds.

## New pure C11 acceptance

`quantapdf_native_clipped_region_budget` authors only C11 owned test
geometry, **not** another external PDF. Its 600×600 page has one red
40×40 Fill and a two-contour, even-odd Clip of two nested triangles.
The proven 40×40 bounding envelope visits at most
`(4 + 6) × 16 × 40 × 40 = 256,000` edge checks. It validates exact
RGB at the white center hole, red outer lobe and untouched page
corners, deterministic repeated output, and nonzero vs even-odd
interior behavior. It also requires refusal and zero output when
`max_sample_tests = 255,999`, acceptance at exactly 256,000,
and **unchanged refusal of the same unclipped Fill** under 8M.

This increments the Native suite **54 → 55 CTests** without a second
renderer or a changed public ABI.

## Explicit non-claims

No change to `tests/external_source_corpus/manifest.json`, the
original 3,747-byte yWorks PDF or its rights/SHA-256 record. Its full
four-Clip, three-state page remains `UNSUPPORTED` through the
existing public Native V10 external-negative tests. So does the
TeXample source. The next work is a separate fully bounded multi-Clip
scope/GS implementation with exact original-source RGB and
conservative **aggregate** work accounting. Old private Graph/PDFium
pixel parity does not count as public Native proof.
