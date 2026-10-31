# Original yWorks: public Native V10 Source→C11 RGB qualification

Parent: exact-head qualified [Draft PR #557](https://github.com/qigao/QuantaPDF/pull/557), HEAD `0a2ccc43d8494c57271033157dcf0f1e8b1826a3`, CI #41026099473 SUCCESS 3/3.

## Source and rendering boundary

The actual unchanged 3,747-byte `tests/external_source_corpus/documents/yworks.clippath-cliprule-v1.pdf`, SHA256 `9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783`, has been independently admitted by the existing QPDF Source V10 parser as **three original ordered PATH paints** (one red 1px `S`, two blue `f`), four definition-time frozen PATH Clip leaves, three sibling Clip intersections and three opaque Normal GS. Every owned C allocation already passes the in-tree NOMEM rollback and recovery matrix.

The new **public Source V10 bridge** selects exactly that previously qualified **3 GS / 7 Clip / 3 PATH / zero Image** shape, and nothing else, into the same private 72-DPI C11 device projection and the one existing V5→V1 4×4 raster. Neither PDFium runtime nor a fallback/alternate parser/Raster can render unsupported source. All existing Clip/GS profiles keep their own selection policy.

## Independent original PDF pixel goldens

At 72 DPI, public RGB from this immutable PDF must be exactly 600×600, 3 channels, stride 1,800 and 1,080,000 RGB bytes. The fixtures assert source-geometry-based control samples:
- White at (20,40); red half-coverage **FF7F7F** at (40,39), (40,40) and (180,40) for the horizontal 1px `S` under W*; W* rectangular hole (100,40) and butt-capped endpoint (400,40) stay white.
- First star under nonzero `W` paints blue at winding-two (80,125), while the third independent star under even-odd `W*` paints blue at (160,125) but leaves winding-two (180,125) white.
- White beyond either star at (250,125), beyond the common outer W at (500,500).
- Three seeded independent public original Document runs each produce byte-identical full RGB, with the first yWorks Bitmap retained after Page/Document/Context destruction.
- Unqualified 96/144 DPI physical width-one Stroke is **UNSUPPORTED/null Bitmap**, not a resized 72-DPI image.

The existing *private* actual-original Source/C11 test independently checks complete RGBA, conservative V1 half-width work lease, budget cutoff **5,391,360 accept / 5,391,359 refuse**, max-edges 38 refuse, detached image owner and real Source allocation-fault rollback. Original global 8,000,000 sample cap and three fixed equal shares stay unchanged.

## Strict external-source/corpus accounting

The established seven-document SHA/license verifier is never modified and still writes **integrity-only evidence (zero native passes)**. The earlier three-document public replay remains **3/9 source document invocations: one blank positive + two negatives per pass**, with its independent anchored fingerprints and reconciliation untouched.

The existing complex-original replay now processes exactly the two prior reviewed originals across three anchored orders: **unchanged yWorks OK/real RGB x3**, **TeXample original UNSUPPORTED/null Bitmap x3**, and **six independently reopened/retired reviewed white-blank recovery renders**. The strict upgraded receipt therefore has **12 executions, 9 positives including blank recovery, 3 refusals, 1 remaining negative original**. Its separate strict Python reconciler independently rehashes all seven originals, checks fixed replay order/fingerprints, per-source status/RGB/atomic refusal, exact counts and corruption-test mutations. This is the **fourth distinct original** with verified public C11 Source semantics (the existing three-document replay remains separate); it does not claim TeXample, fonts, general clipping or broader Stroke.

All original PDFs and historical PDFium-only provenance remain byte-for-byte unchanged; no installer, consumer CI, second renderer or public SDK ABI change. TeXample remains an Open gap under Issues #213/#214/#233/#530; the complete PR stack stays Draft/unmerged.
