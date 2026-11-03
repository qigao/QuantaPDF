# M2b.2o: original compound Text/Fill/Image Source ownership

This Source increment admits one precise original sequence: q saves GS0,
rectangular W/W* plus n defines leaf1, another q saves leaf1, and a second
rectangular W/W* plus n defines their intersection before BT. The ordered
opaque Text→Fill→Image paints use that same frozen root; Q returns to leaf1,
then Q returns to GS0. No paint after either restore is admitted by this
profile. Both Clip frames, an optional Text q frame retaining the intersection,
and the BT handoff CTM are identity. The saved Clip IDs are exactly [0,1] or
[0,1,2]; the original Text frame does not create a new Clip resource.

The existing pre-BT image/content reader already retains independently frozen
Clip leaves when a saved frame can observe leaf1. The same outline-reader
handoff now qualifies this exact nested stack and carries staging identity 2
through its actual q/Q callbacks. Existing Text/Fill/Image callbacks preserve
that membership. At complete original EOF, the shared three-paint materializer
and existing Clip graph publisher own two PATH leaves plus the original
Intersection(1,2) root. One opaque Normal GS names root3; each operation names
GS1. Original leaves are neither replaced by a reduced rectangle nor copied
into a second parser or renderer. Old singleton/sibling allocations remain
unchanged.

Genuine QPDF originals 126/127 use RAW/Flate Image samples and swap W/W* rules.
The existing Source/OOM executable checks exact leaf commands, independent
payloads, root member IDs, complete GS/operation bindings, glyph/Fill payloads
against the accepted original single-root source, decoded Image bytes and
full authored placement. The IR remains valid after input PDF bytes retire.
The real C++ saved-stack allocation at the BT handoff and every reached
Source C allocation are faulted, including each leaf/root/member publication.
Failures clear all IR fields; structural retries and disposal remain exact.
Source operations, source pixels and full Image placement have N/N−1 checks
(3/2, 4/3 and 192/191).

Missing/excess Q, a third root, changed handoff CTM, paints after either
restore, a post-BT second Clip, missing n, reverse order, extra Font alias,
unknown Do, skew/cubic leaf, unqualified saved-stack shape, off-lattice leaf
and an original without Text refuse the
complete original. Prepared metadata and actual public Bitmap calls still
refuse both positive compound originals at 72/96/144 DPI; the existing public
test then recovers accepted single/sibling originals in the same Context.

This is Source ownership qualification. The subsequent
[M2b.2p private V5 stage](native-m2b2p-compound-v5.md) adds shared compilation
and independent correlated sample references. Public Bitmap pixels still need
separate fault/receipt and lifetime acceptance. Existing public ABI, goldens, numeric caps, test targets,
registrations, CMake/workflow and timeouts are unchanged. #584/#580/#570/#208
remain open; exact candidate CI and independent master CI after merge are
separate gates.
