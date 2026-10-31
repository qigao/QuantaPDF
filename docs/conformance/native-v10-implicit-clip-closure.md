# Source V10: bounded return-to-start Clip subpath closure

Parent: qualified Draft [#553](https://github.com/qigao/QuantaPDF/pull/553).

In the original PDF, `m` and `re` start a new subpath and the
preceding path participates in subsequent `W/W*` winding as if
closed. The first independently admitted increment is **narrower
than arbitrary PDF implicit closure**: native Source V10 recognizes
only a fully formed first contour whose LAST original `LINE_TO`
returns **exactly** to the contour's `MOVE_TO` point. This is a
real pattern in the original yWorks W* Clip source.

## Exact admission and failure contract

The sole existing Source V10 parser may append one internal
`PATH_CLOSE` before beginning the second contour after proving the
source's last `l` returned to its first `m`. This only replaces a
duplicate closing edge: the original line to the start remains,
so no source geometry, winding or painted pixels are lost.

The preflight still proves the original COMPLETE first contour,
exact endpoint identity, maximum two contours and the original
**nine commands total** including the implicit internal CLOSE and
next contour's minimum. Arbitrary unreturned triangles, degenerate
contours, a third `m`, over-quota `re`, malformed operators and late
unsupported paints stay **UNSUPPORTED/null Bitmap**, as do all older
private Source signatures. Output is not published on any failure.
No other parser, renderer, PDFium runtime/fallback, public API
or cap change.

## Real source PDFs in the existing CTest

`quantapdf.native_v10_wstar_compound` now checks **five** extra
public RGB positives: original `l` returning exactly to the first
`m`, followed by a second `m` under W/W*, including reverse inner
winding and final implicit `W* n` closure. Six extra negatives
prove unreturned first contours still reject (legacy tests #31/#38),
insufficient first edges, ten-command overflow, overflowing second
`re`, a third contour and Text after paint. Total is **11 RGB-positive
/ 13 atomic-negative** original generated PDFs, extending baseline
6/7 without changing any historical test acceptance.

## Original yWorks external source remains unsupported

The untouched 3,747-byte original yWorks
`tests/external_source_corpus/documents/yworks.clippath-cliprule-v1.pdf`
SHA-256 `9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783`
also has a larger first W* Clip (exceeding this nine-command total),
plus three independent q/Q branches and Stroke→Fill→Fill.
The source reader still cannot publish that original's 3GS/7Clip
graph; it remains public V10 **UNSUPPORTED/null Bitmap**.
Related Issues #213/#214/#233 remain Open, all PRs Draft/unmerged.
