# M2b.2n: public sibling Text/Fill/Image preparation and Bitmap

The existing public Source V10 path now prepares the exact Source-owned
three-paint sibling graph: Text and Fill retain GS1/Clip1, Q restores GS0,
and a new q plus rectangular W/W* completed by n binds Image to GS2/Clip2.
Both identity-space leaves and opaque Normal states must be present and used.
Only Text→Fill→Image with original IDs `[1,1,2]` qualifies. The allocation-free
metadata classifier uses the same rectangle predicate as the previous
single-root classifier. The existing resource closure inventories both leaves
and states; the selected route calls the already qualified M2b.2m compiler.

No new parser, raster, public ABI or Context ownership mechanism is added.
Source publication, one address-stable compiled owner, the global ledger,
execution receipts, detached RGB storage and Context leases all use the same
existing path. The 8M sample/512 edge ceilings, Image/Fill 1/8 shares, four-paint
ceiling, source/full-placement/output-pixel and memory accounting are unchanged.

Original RAW/Flate fixtures 113/114 swap their two W/W* rules. At 72/96/144 DPI,
every public RGB byte is compared against original-quadratic glyph sample
planes and analytic Clip/Fill/Image coverage. Image samples exclusive to each
root are nonempty; binding Image to Clip1 or manufacturing Clip1∩Clip2 must
visibly change output at every DPI. All six sibling Bitmaps remain readable
against independent bytes after Page, Document and caller Context teardown.

The existing Context/OOM executable exercises both original siblings at all
three DPIs: real Bitmap storage refusal, Bitmap and prepared-Source reference
overflow, balanced references, same-Context full-byte recovery, and final
Context destruction after retained Bitmap disposal. The Source/OOM executable
checks the new prepared route and resource masks, every reached C11 allocation
and Source C allocation at 96 DPI, empty output and receipt on failure, and
exact same-IR retries. Original Source IR snapshots remain unchanged. N/N−1
checks cover operations, source/full-placement/output pixels, peak bytes and
sample/edge admission under unchanged shares.

Missing/excess Q, nested/third or unused roots, late Text/Fill, nonidentity CTM,
missing n, unknown Do and extra Font alias still refuse atomically. The public
test recovers both siblings in the same Context after these malformed originals.
Eight malformed borrowed graph variants also refuse metadata preparation;
the private compiler still refuses them before geometry allocation.

Existing test executables, registrations, deadlines and golden assets remain
unchanged. This bounded candidate does not qualify compound Text Clip roots,
reverse sibling order, additional fonts/paint orders, arbitrary matrices or
general transparency. #584/#580/#570/#208 remain open. Acceptance requires
exact candidate CI; production acceptance additionally requires independent
master CI after merge.
