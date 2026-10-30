# Source V10: bounded implicit Clip subpath closure

Parent: qualified Draft [#553](https://github.com/qigao/QuantaPDF/pull/553).

The **original PDF** operators `m` (new subpath) and `re` (new
rectangle subpath) end a preceding open subpath. For a `W` or `W*`
Clip, the earlier subpath participates in the fill as if closed,
even without an explicitly authored `h`. The previous Source V10
reader rejected every second `m`/`re` unless the preceding contour
was explicitly closed, which excluded valid original PDFs.

## Admission and ownership

The sole existing QPDF Source V10 parser, and only that source
version, may append an internal `PATH_CLOSE` before beginning the
second contour. It must validate the complete first contour before
mutation, prove the original combined **nine-command total** including
both future `CLOSE` commands and the minimum size of the next
subpath, and still reject a third contour, invalid/missing operators,
zero-length malformed contours, overflow, or late unsupported paint.
No other source signature or C11 raster is expanded. Definition-time
CTM/coordinates and original winding rule are unchanged. The existing
Source -> detached IR -> V5/V1 C11 raster stays unique; all rejected
public calls return no Bitmap and no partial pixels.

## Original-input qualification

The existing `quantapdf.native_v10_wstar_compound` CTest creates
**actual source PDFs** and now checks five additional accepted cases:
implicit first triangle before second `m` under W and W*; explicit
return-to-start `l` followed by another `m`; and second `re`
under W*. These share analytical red/blue RGB controls with the
previous explicitly closed form. Four additional negatives enforce
missing first geometry, an over-budget ten-command Clip, third
contour and a late Text operator. The previous 6 positive / 7 negative
cases remain intact; expected combined count 11 positive / 11 negative.

## Authentic external yWorks remains unsupported

The unmodified 3,747-byte yWorks
`tests/external_source_corpus/documents/yworks.clippath-cliprule-v1.pdf`
(SHA-256 `9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783`)
contains a larger first compound W* Clip, three q/Q branches, and
a Stroke followed by two Fills. This PR does **not** increase Source
V10's nine-command lease or add its missing owned 3GS/7Clip source
materialization; the authentic source remains UNSUPPORTED/null Bitmap.
Keep #213/#214/#233 OPEN; all stacked PRs Draft and unmerged.
