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
Font alias refuse the whole original. Existing public tests refuse both
positive Source graphs at 72/96/144 DPI and then recover in the same Context
against the already accepted four-paint RGB oracle.

**No sibling Bitmap admission:** prepared metadata and actual public rendering
remain unsupported for these inputs. No new C11 primitive, public ABI, numeric
cap, test project, CMake/workflow or timeout. Candidate acceptance still needs
exact HEAD CI and independent master CI after merge. #584/#580/#570/#208 stay
open. The next gate is shared V5 compilation with per-operation frozen roots,
one global ledger and independent full-image references before public admission.
