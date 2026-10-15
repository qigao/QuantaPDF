# #269 — 96 DPI source-backed per-edge / Q4 quantization ledger

This experiment is **test-only**, stacked on [Draft #350](https://github.com/qigao/QuantaPDF/pull/350); no changes to page geometry, C11 raster algorithms, rendering ownership, public SDK, or external corpus. Every input is a **test-generated genuine PDF**, not a separately published and rights-cleared PDF.

## Evidence to explain

The already qualified original-PDFium/C11 comparison at 96 DPI found 680 different pixels across four `/UserUnit 7` pages (170 pixels per intrinsic /Rotate). No mismatch was farther than 1.5 pixels from the theoretical projected red rectangle [4,9]×[5,9]. Parent #350 showed that ideal rectangle overlap, under either integer-grid fit or nominal DPI, does **not** yet match PDFium.

## Independent numerical probes

The child augments the same immutable, bounded 12-frame report with these **96-DPI-only** measurements; all 72-DPI strict zero-RGB tests, 144-DPI observations, 36 zero-output negatives and original source byte repeat/IR immutability checks still execute.

- **Five disjoint edge bins** for every changed pixel: left, right, top, bottom, and corners. Pixels farther than 1.5 device pixels from theoretical boundaries retain the existing `off_ideal_edge_differences` count. Each differing pixel must be counted exactly once.
- **Directional coverage bias**: count pixels where the PDFium green value is smaller (more red) than the white-composited C11 result, and vice versa. Detect PDFium RGB not representable as red-on-white by recording unexpected R channel or unequal G/B values; do not assume a result.
- **Partial coverage count**: count original PDFium and native C11 pixels with green strictly between 0 and 255, separately.
- **Independent Q4 sub-center coverage**: evaluate the axis-aligned theoretical source-intersection rectangle against all 4×4 device subcenters; round coverage using the native V1 sample denominator (16) to a green channel prediction. Report its RGB distance to *both* the already-produced C11 image and the independently rendered PDFium source. This is numerical evidence only; it neither renders a competing bitmap nor invokes a fallback.
- **PDFium distance from nearest admissible native Q4 green level**: for every 96-DPI reference pixel, measure the integer distance to the nearest of the 17 native 4×4 coverage-quantized green values. This helps separate geometric edge position from sample quantization without guessing a private PDFium algorithm.

All extra loops remain bounded by the existing 94×75×4 96-DPI pixel grid and 16 subcenters per pixel; no new large raster allocation, source re-export, or provider linkage is added. JSON top-level totals must reconcile exactly with each per-rotation frame and its edge bins. The strict 72-DPI RGB 0/0 gate is unchanged; **96-DPI status remains diagnostic regardless of whether any new candidate reduces RGB error**.

Cross-platform acceptance requires Linux Release, Linux ASan/UBSan, Windows, and macOS exact-HEAD CI JSONs. If Q4 numerical values differ from actual C11, investigate float narrowing or clip sample semantics before changing the coordinate affine. If PDFium uses non-Q4 green levels, isolate coverage/quantization in a *separate* test-only A/B rather than modifying production.

#269 and #343 remain OPEN; all stacked PRs **Draft / DO NOT MERGE**. The independent reporter `Userunit.pdf` bytes and redistribution rights remain unverified. Formal external-native positives unchanged; public/installed SDK PDFium and #208 backend cutover blocked.
