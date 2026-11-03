# M2b.2l: original sibling Clip Source ownership

This private Source increment admits one precise original sequence: Text and
Fill under Clip1, real Q to GS0, q plus a new rectangular W/W* completed by n,
one RAW/Flate Image under independent Clip2, then Q. Both leaves remain
separate original resources. Operations retain `[1,1,2]`; GS1/GS2 name Clip1
and Clip2. No intersection rectangle or replacement of the first leaf is
manufactured.

The existing post-BT token reader gains W/W* completion and calls back to the
same Source owner. The actual graphics-state stack carries the returned Clip
identity. The new leaf requires identity CTM, restored GS0 and the complete
qualified Text/Fill prefix. Complete original EOF and resource closure are
validated before the existing three-paint materializer copies either leaf.
It reuses the prior Clip graph publisher and image/glyph owners. The old
singleton allocation order remains unchanged.

Existing Source/OOM acceptance uses genuine QPDF originals 113/114 with
opposite W/W* leaf rules and RAW/Flate samples. It checks both original
rectangles, unique ownership, exact glyph/Fill preservation against the
unchanged first-scope original, complete decoded Image bytes and placement,
and survival after original input bytes are released. It fails the real C++
retained-leaf allocation and every reached C allocation, requiring NOMEM,
empty IR and exact structural recovery. Authored operations, unique source
pixels and full image placement have N/N−1 checks (3/2, 4/3 and 192/191).

Missing/excess Q, a balanced nested second root, a third root, late Text/Fill,
nonidentity Clip CTM, unused second root, missing n, unknown Do and an extra
Font alias refuse the whole original. This Source stage originally refused
public rendering; subsequent [private V5](native-m2b2m-sibling-v5.md) and
[public preparation](native-m2b2n-public-sibling.md) stages qualify the same
two owned graphs, with independent bytes and same-Context recovery.

This stage establishes Source ownership. No new C11 primitive, public ABI, numeric
cap, test project, CMake/workflow or timeout. Candidate acceptance still needs
exact HEAD CI and independent master CI after merge. #584/#580/#570/#208 stay
open for wider Clip graphs, orders, fonts and matrices. The following stages
reuse these per-operation frozen roots and one global ledger.
