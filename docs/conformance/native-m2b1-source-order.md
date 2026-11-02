# M2b.1 — Source-only ordered Text/Fill/Image checkpoint

Tracks [#582](https://github.com/qigao/QuantaPDF/issues/582), parent [#580](https://github.com/qigao/QuantaPDF/issues/580). Based on independent M2a master accepted `affc4132da73c574fd4f17da313c1a337cbde5bc`, CI #2309 61/61 native and installed 1/1.

## What this **does** add

One original QPDF Content token stream now stages a **completed Fill event** from the **existing** embedded-TTF outline interpreter. It preserves the original Source operation kind sequence `TEXT_SHOW, FILL, IMAGE` or `IMAGE, FILL, TEXT_SHOW`, including the path prefix before the first BT. Whole-content EOF is mandatory. One native owner copies each opaque black TTF glyph contour, one frozen opaque green rectangle Fill and the real RAW Image bytes into a unique three-operation IR. No QPDF/font handle escapes. Existing Text-only and two-paint M2a owners retain their prior exact allocation/geometry/bitmap paths.

New original fixture modes `66` and `67` are genuine PDF pages (one resource Font and one XObject, 128×64 MediaBox). Each has one q/Q-contained nonzero Fill `12 20 16 20 re f` and one black TTF Hello show plus RGB Image 2×2 at original bounds `10 15 16 12 cm` under their actual authored ordering.

## What remains deliberately closed

Public C11 geometry/Bitmap for these **three** paints still returns `UNSUPPORTED`; the current M2a V5 route accepts only two logical paints. The existing native Source/OOM target validates owned three-op IR and atomic C11 refusal. The public native target verifies no partial Bitmap. No new parser, C11 raster, second source-over compositor, CMake/CI/consumer project, ABI, PDFium or historic golden changes.

## Next qualification gate before enabling Bitmap

Integrate PATH's existing C11 V1 geometry compiler as a **third V5 layer** with Text+Image under one page-owned sample/edge/byte ledger. Use genuinely independent pre-M2a Roboto RGBA and analytic 4×4 rectangle Fill/image reference for exact 72/96/144 whole-image RGB in both orders; retain M2a's alpha-rounding behavior and N/N−1/real malloc/Context rollback. M2b.2 W/W* correlated Clip sampling remains a separate gate. Keep PR Draft; no stage success without exact-head CI.
