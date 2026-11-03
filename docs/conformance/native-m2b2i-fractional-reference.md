# M2b.2i: original fractional Text/Clip reference

This private acceptance increment addresses the independent-reference gate
in #584. Genuine QPDF original fixtures 99/100 paint embedded Hello Text,
partial green Fill and the same RAW RGB Image under W/W* with authored
quarter-point x/y Clip edges. Both source paint orders use the existing
shared typed operation compiler, one V5 owner/ledger and 4x4 sampler.

The offline original-glyf reference now exports five separate 16-bit glyph
sample planes. It uses the existing pinned font, original quadratics and
60-digit Decimal roots, independently of production Source outline
conversion and C11 sampling. 80-digit generation produces identical data.
All 24 historical RGBA references remain byte-identical; each new sample
plane also collapses to the old Hello coverage with original glyph order.
The binary format and SHA-256 values are in the goldens README.

At 72/96/144 DPI the existing Source/OOM executable intersects each glyph
plane with an independently calculated Clip mask, preserves per-glyph
white-background rounding, then composites Text/Fill/Image in source order.
It checks every RGBA byte. Nonempty disjoint marginal masks and nonempty
partial intersections must both occur. A deliberately wrong rounded-alpha
product must produce different visible output, including exposed Text.

The new original at 96 DPI also exercises every reached ledger-backed C11
allocation fault, zero output/reservations/live bytes on failure, and exact
same-IR recovery against the independent expected image. Numerical limits,
historical 40s CTest timeout, test registration, ABI and production code are
unchanged. FontTools remains an offline authoring dependency only.

Public preparation still refuses these fractional originals at every
requested DPI. Public Source/Context/Bitmap integration and its fault/quota
acceptance remain the next independent gate. #584/#580/#570/#208 stay open.
