# #269 — Original jsPDF edge residual attribution after corner A/B

**Draft / DO NOT MERGE — diagnostic only.** Stacked on #287, exact
HEAD `d03e1355a2e18f702c138cc1f1454ad0b4ab75ce`.
Three-platform full CI #38472220444 succeeded.

## Measured original baseline

The immutable 3,344-byte upstream jsPDF PDF has original SHA-256
`179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e`.
Its private corner-aware C11 candidate, composed with unchanged later
W-clipped Fill and compared against original PDFium at 596x842, has:
26,955 absolute RGB error, 1,060 changed pixels, zero >64 severe
pixels. Existing attribution splits this into 879 Stroke-partial
changed (23,482 error), 80 Fill-partial (2,862 error) and 101 flat
(611 error). This is not parity qualification.

## This trial: no renderer change

A new full-page **read-only** re-scan partitions each original pixel
into seven exclusive test-only regions: bottom single-pass Stroke,
right and left vertical Stroke, top repeated Stroke control, x=150 red W Clip
edge, weak y=100 W Clip fringe, and all other pixels. Region names are
literal original-source observation windows with native per-layer alpha
guards, *not* production heuristics or PDFium internals.

The report includes every region's pixel/changed/gross counts, RGB
absolute-error contribution, maximum-channel delta and worst
coordinate. It also emits original PDFium RGB, C11 RGB and the two
C11 layer alphas at exact witnesses (50,139), (250,139), (150,21),
(100,100) and (250,19).

An independent CMake checker proves all seven bins partition 501,832
pixels and reconcile to the existing whole-page error/changed/gross/max
totals, verifies five anchored source categories and RGB bounds, and
retains the SHA/non-admission contract. The report explicitly keeps
pixel_parity_qualified=false. No improvement threshold is invented.

**Next:** use these results to choose between single-pass Stroke
coverage and W Clip edge alpha as the next *independent* A/B hypothesis.
All three existing opt-in preview modes, default C11 raster, public
PDFium SDK, source bytes and corpus positive counts remain unchanged.
Keep #269 Open and #287/#288 Draft / DO NOT MERGE.
