# M2b.2k: fractional Clip with a Q-restored fourth Fill

The strict public Text→Fill→Image prefix under one frozen axis-aligned W/W*
root now accepts fractional Clip boundaries when followed by exactly one
Q-restored GS0 opaque red rectangular Fill. The complete prefix/suffix shape,
resource closure and original operation order are unchanged. The temporary
pixel-alignment predicate is removed; the existing shared typed operation
compiler, V5 owner/compositor and global ledger execute all four paints.
The suffix still receives half of the same 8M sample cap and eight edges.

Original fixture 105 preserves the previously refused W fractional Clip and
the red suffix wholly outside it. New W* fixture 106 has a quarter-point
suffix crossing the Clip boundary and overlapping Text, green Fill and RGB
Image. It proves both actual GS0 restoration and the last paint's source
order. At 72/96/144 DPI every RGB byte comes from the existing independent
original-quadratic glyph masks and analytic Clip/Fill/Image/suffix samples.
The wrong averaged-alpha product must still create visible spurious ink;
the crossing suffix must contain samples both inside and outside the Clip.
No original glyph mask or historical RGBA golden changes.

Existing native tests additionally cover:

- Both W/W* four-paint originals at 96 DPI: every reached Source C and
  ledger-backed C11 allocation fault, empty IR or zero image/receipt,
  independent complete-byte retries, and N/N−1 source/output/drawn pixels,
  operation count and peak bytes. Sample/edge admission thresholds are
  measured under the unchanged child shares and checked at N/N−1.
- Actual public Bitmap storage NOMEM and both Context retain-overflow
  positions at each DPI, balanced references, independent full-byte retries,
  surviving Bitmap pixels and exactly one final Context destruction.
- Original excess Q, late W, skew/cubic suffixes, fifth paint and reverse
  four-paint order refuse atomically. The same Context then reproduces the
  crossing-suffix image; its retained 96-DPI Bitmap survives all input handles.

This is candidate-branch acceptance until exact HEAD and independent master
CI pass. No new public ABI, parser, raster, test project, CMake/workflow,
numerical limit or timeout. #584/#580/#570/#208 remain open for sibling or
compound roots, wider original orders, multiple fonts and matrices.
