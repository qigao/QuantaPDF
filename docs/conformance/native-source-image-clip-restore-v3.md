# Native Image Clip Restoration V3 — first Do clipped, second Do after Q

Tracks [#427](https://github.com/qigao/QuantaPDF/issues/427),
[#214](https://github.com/qigao/QuantaPDF/issues/214),
[#154](https://github.com/qigao/QuantaPDF/issues/154) and the complete
PDFium runtime replacement gate [#208](https://github.com/qigao/QuantaPDF/issues/208).

## Narrow source semantics

The new private source function
`quantapdf_qpdf_source_restored_clipped_raw_images_to_ir_v3()`
accepts **exactly two** ordered Image XObject `Do` operations and **one**
strictly admitted `W` or `W*` Clip leaf. The first Image executes while
the Clip is active. A genuine balanced `Q` must then **pop the Clip**,
and the second Image executes with graphics-state and Clip ID **zero**.
In contrast, V2 continues to require both Images under the same frozen
Clip, and V1 continues to require exactly one Image.

The complete original decoded `/Contents` sequence is interpreted.
Valid operators in this profile are balanced `q/Q`, positive
axis-aligned/integer-placement `cm`, one `m/l/h/re W|W* n` Clip
definition, and the two referenced `Do` Image operations. Unsupported
text/painting/color/Group/Form/GS/mask/inline-image operations, extra Do,
pop-before-first, second Do before Clip restoration, invalid CTM or
source budgets reject the **whole page** with zero published IR.

Both Image resources are separately copied to owned RAW Gray8/RGB24
buffers, even if they reference the same source Image name. The Clip
PATH geometry and nonzero/even-odd rule are frozen at its definition
transform. Each Image receives its own checked placement. Source/qpdf
owners are released before independent native C11 raster.

## Independent Native Raster

`quantapdf_ir_raster_restored_raw_images_v3()` is a strictly separate
private C11 entry point, built with the existing V2/V1 Image/Clip
renderers. It requires two image draws, two resources, one frozen Clip
PATH and one GS; Image #1 has GS ID 1 and Image #2 GS ID 0.

The first Image is rendered with V1's 4×4 per-pixel Clip coverage.
The **second Image is rendered unmasked** by the already-qualified
RAW Image painter, then both detached transparent layers are composed
in original order onto the page's opaque or transparent background.
Both source-pixel and drawn-pixel budgets are preflighted in aggregate.
Only the first paint spends Clip edge/sample budget; all intermediate
raster allocations are disposed on failure, and **no partial output is
published**. V2's two-clipped contract and global budget partition
remain unchanged.

The test-only C11 target
`quantapdf.native_ir_raw_image_clip_v3` links **neither qpdf nor
PDFium**. It qualifies opaque/transparent backgrounds, source-over
order, visibly painted pixels outside the old Clip after restoration,
exact repeated RGBA, invalid GS/image/resource IDs, late payload errors
and independent budget failures.

The original-PDF test uses independent QPDF-authored source bytes and
direct test-only PDFium original-source whole-page RGB/explicit color
probes, including W, W* split across streams, distinct definition CTM,
valid post-Q outside-Clip painting and strict poisoned-output negatives.
The original source is disposed **before** native rendering.

This is **not** the public page renderer. Arbitrary multiple
save/restore Clip leaves, mixed Path/Image/Clip, Form/image masks,
transparency groups and PDFium-free production package are still outside
this bounded profile and remain tracked under #214/#145/#154/#208.

The formal licensed external PDF corpus stays at **7 originals /
2 W* original-source native positives**. These generated qualification
cases are not new independently published originals.
