# #269 — Observe original PDFium binary rectangle boundaries at 96 DPI

**Test-only**, precisely stacked on [Draft #352](https://github.com/qigao/QuantaPDF/pull/352); no changes to Graph, Composer, native Raster, ownership, SDK, ABI or external corpus.

Parent #351 established that the native C11 4×4 Q4 coverage is exactly reproducible from the real test-generated PDF source geometry, while original PDFium at 96 DPI has **zero partial green pixels**. Parent #352 measured five predetermined integer edge-snap hypotheses; even the best (floor all edges) had 152 differing pixels across four intrinsic rotations, although the 180° case was exact. Thus a global floor/ceil/round rule cannot be adopted from the existing evidence.

## Pixel-observed boundaries, no additional painting

The new diagnostic scans **only the already-rendered original PDFium 96-DPI RGB bytes** in each of the four QPDF test-generated source pages. A red pixel is precisely one with green channel equal to 0. The parent records any non-red/white color and partial coverage independently. For each rotation, calculate:

- the **exclusive integer rectangle** `[left, top, right, bottom)` enclosing all PDFium red pixels;
- the number of opaque red pixels and whether the rectangle has exactly the same number of pixels as its enclosed integer area (independent confirmation of a filled binary rectangle, not an assumption);
- four signed integer differences of the actual red rectangle edges from the `floor()` of the independently projected original Source→Device rectangle bounds; these are *observations*, not chosen candidate rules;
- aggregate total red pixels and number of binary rectangular PDFium frames, separately from the existing Q4/native/PDFium RGB A/B results.

Every bound must be inside the already bounded 94×75/75×94 device grid; all values are recorded in the same atomic JSON. CTest reconciles per-frame red counts and rectangle flags with top-level totals and rejects malformed negative dimensions, unbounded edge offsets, absent data or non-96DPI bleed-through. It does **not** use the PDFium-observed rectangle to choose or render another candidate bitmap, and it does not modify a pixel in the private C11 output.

The point of the test is to reveal whether PDFium snaps **specific edges** differently by intrinsic rotation or original clip/paint boundary. If the observed box is not a filled rectangle, the hypothesis fails and must not be promoted. Even an exact four-rectangle observation from these generated fixtures is not evidence for arbitrary source PDF raster compatibility; any actual renderer candidate requires a separately gated, private, bounded path and more independent source producers.

Parent 72DPI strict zero-RGB, 144DPI exact observed parity, 36 transactional zero-output negatives, input-byte repeat, IR immutability, original PDFium byte repeat, private Q4 and five snap-policy A/B remain unchanged. All original inputs are QPDF **test-generated**; the independent reporter PDF under #343 remains unverified for source bytes and redistribution rights.

Keep #269/#343 OPEN, every stacked PR Draft / DO NOT MERGE, external original-positive count unchanged, installed/public SDK PDFium and #208 cutover blocked.
