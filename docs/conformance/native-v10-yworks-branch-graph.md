# V10 original yWorks q/Q branch topology — private C11 proof only

Stacked on exact-head qualified Draft [#551](https://github.com/qigao/QuantaPDF/pull/551).
Read the unchanged 3,747-byte original yWorks
`tests/external_source_corpus/documents/yworks.clippath-cliprule-v1.pdf`
(raw SHA-256 `9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783`).
Its uncompressed PDF content stream has:
- W on the full-page rectangle, shared across three nested q/Q paints.
- W* with **two authored rectangular subpaths** before red `S` (a one-point-wide straight horizontal Stroke);
  one subpath closes by returning to its start with an explicit line,
  not a standalone `h`. PDF W/W* semantics treat it as closed.
- W on a five-vertex star before the first blue `f`.
- W* on a shifted five-vertex star before the second blue `f`.

Thus the real source has **1 Stroke + 2 Fill**, 4 Clip leaves, three
different active q/Q branch intersections. With a canonical shared leaf
1, the *equivalent logical topology* is 5={1,2}, 6={1,3}, 7={1,4},
and 3 GS records select roots 5,6,7; canonical **IR resource numbering
itself remains unproven** until the Source reader is qualified.

The new exact *private Fill-only IR* C11 test proves this branch topology,
with independently red/blue/green **surrogate Fills**, not a reproduction
of the original red Stroke. It uses the existing V5->V1 4x4 sampler
with each of 3 operations billed <=floor(original quota/3).
The conservative synthetic W* bill is 163072; 3-way cap 489216 admits,
489215 rejects. Other branches bill 69120 and 57600. Existing 8M
global cap, resource ownership and historical entrypoints are unchanged.

**Still UNSUPPORTED/null Bitmap** in the public Source V10 corpus:
the original Stroke inside two Clips, its PDF open-subpath closure,
definition-time CTM, exact original source Graph/GS numbering and
complete external RGB output require separate implementation and
acceptance. No PDFium runtime, fallback or second raster is added.
Keep #233 and #214 OPEN, PRs Draft/unmerged.
