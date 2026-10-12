# External W* original-source candidate screening (#262)

Status: **screening only; no new external positive**. Tracks
[#262](https://github.com/qigao/QuantaPDF/issues/262) after the original
`W n` stroke-pixel gate in #254/#261 passed. This document is *not* a
redistribution/license approval, a PDFium-vs-native pixel result, or an
extension of public SDK compatibility.

## Pinned upstream and screened surface

- Independent publisher: [PDFium](https://github.com/chromium/pdfium).
- Repository revision: `a84323421e94f484faca52dd9d027934eba42ab8`
  (the same immutable revision as the accepted `clip_path.pdf`).
- Scope: `testing/resources` at that revision; 194 `.in` files with
  published `.pdf` counterparts were checked for explicit standalone
  `W* n` tokens (one template could not be UTF-8 decoded), and 62
  additional published `.pdf` files lacking paired `.in` were inspected
  for literal, uncompressed `W*` tokens.
- The independently published `testing/resources/pixel` subdirectory was
  additionally screened at the same revision: 24 tracked original `.pdf`
  files were checked for literal raw `W*`; one text-bearing candidate was
  found. Two of those PDFs advertise `FlateDecode`, so this probe does not
  establish completeness for compressed streams.
- These are **candidate probes**, not a complete search: compressed PDF
  content streams, non-line-separated operators, nested directories and
  unparseable files can escape text scanning. A complete page-content
  parser/preflight is mandatory before admission. `.in` templates
  themselves are not external source PDFs.

## Exact original PDF candidates requiring rejection

The following are independently published **unchanged `.pdf`** blobs
with actual `W* n` observed in the original content stream. The immutable
Git blob identifies candidate bytes; a future admission still requires
raw-byte SHA-256 and document-level redistribution review.

| Original PDF (at revision above) | Git blob SHA-1 | Actual page content / why unsupported |
|---|---|---|
| `testing/resources/bug_1574.pdf` | `df274187155e8c02b86b1e51eb3347e29759fad4` | `q`, rectangular `W* n`, opaque `f*`, then nested `q` and `BT/Tj/ET` text. Cannot omit the text to claim an original-source positive. |
| `testing/resources/tagged_table.pdf` | `6e2bf9e4967b85939336da81e64df795ffd52037` | Two `W* n` scopes followed by marked content and `/Im7 Do`, `/Tr8 Do` XObjects. A fill-only or stroke-only native path cannot reproduce the original image. |
| `testing/resources/tagged_table_bad_parent.pdf` | `43d9aa38a4e4ec416d0cf4c22a868b1ffb0d198f` | Same XObject/marked-content blocker; not an independent positive primitive. |
| `testing/resources/bug_601362.pdf` | `d21a83405ea6469ec329ab31ad207362fcdd6706` | Repeated `W* n` scopes followed by `BT/TJ/ET` and graphics-state operations. |
| `testing/resources/text_font.pdf` | `78f3bcc784a08d0abf0948d78c52881fafca6d49` | `W* n` followed by font-dependent `BT/TJ/ET` text. |
| `testing/resources/pixel/bug_925736.pdf` | `429d53a477fc436692a00c08a2a7b4e867e1cce3` | Original uncompressed `W* n`, followed by `BT`, font-dependent `TJ`, and `ET` text. It remains outside the current Path-only subset. |

All sources above remain **unadmitted**. Do not commit them to the
external corpus, add a permissive license tag by inference, or increment
`native_path_clip_supported_count` / a W* counter based on a parsed
`W*` token alone. Generated fixtures and original-source evidence are
separate populations.

## Native preparation in this branch

`quantapdf.native_scoped_clip_dag_v1` now exercises a provider-free C11
three-operation scenario in which a nested even-odd **two-contour hole**
suppresses red paint, later blue paint is scoped to the restored outer
Clip, and green is unrestricted. The same two contours must differ
visibly when changing `W*` to nonzero `W`. Repeat RGBA identity and
invalid rule atomic rejection are checked. This is *not* evidence that an
independently published W* PDF passes the source Graph/IR path or PDFium
pixel oracle.

## Next acceptance gate

1. Find a byte-pinned, separately license-reviewed, published **original
   PDF** with true `W*` (ideally nested `q/Q`) and only the currently
   supported opaque Path operations, or scope a separate primitive
   expansion first. Preserve complete original source bytes.
2. Preflight every operator and resource on the selected page, including
   graphics-state changes and late operations; fail closed with no
   partial detached IR for text, images or XObjects.
3. Independently render **the original source file** with PDFium, not a
   Composer export; verify source Document/Graph are disposed before
   native render, fixed interior goldens, whole-image RGB/gross-error
   thresholds and deterministic repeated RGBA bytes.
4. Publish a distinct reconciled external `W*` positive count only with
   exact-head Linux Release+ASan/UBSan, macOS, Windows and installed SDK
   evidence. No public ABI changes, no PDFium fallback or SDK cutover
   (roadmap #208).

Current external W* positive count: **0**. Reconciled external semantic
JSON now distinguishes `native_w_clip_supported_count: 1` from
`native_wstar_clip_supported_count: 0`; attempts to report a positive W*
count without a qualified original source must fail closed.
