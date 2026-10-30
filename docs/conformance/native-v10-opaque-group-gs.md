# Public Native V10: bounded opaque gs + isolated DeviceRGB Group

This is the **production Source V10 admission** needed for the pinned
`tests/external_source_corpus/documents/pdfium.black-original-v1.pdf`,
not a second PDF renderer, an assumption that arbitrary PDF transparency is
implemented, or a copy of the old Graph/Composer-to-C11 implementation.

The reviewed original is the unchanged Cairo 1.13.1-produced 923-byte
`black.pdf` (SHA-256
`41974886a825bb43056bc121a96780577b4f537c7776e705e485c55a1230bf4d`).
Its page is 612×792, paints a solid black PATH using normal `/a0 gs`,
and declares an isolated DeviceRGB transparency `/Group`.
The only native output is the existing single C11 renderer's opaque PATH
pixels; test-only QPDF may inspect or author original fixture bytes, never
provide output pixels or fallback.

## Exact opt-in /Group

Only public V10 may observe Page `/Group`. It must be a dictionary with
only `/Type`, `/S`, `/I`, `/K`, `/CS`. The qualified profile is:
`/S /Transparency`, **explicit** `/I true`,
`/K false` or omitted, `/CS /DeviceRGB`,
and optional `/Type /Group`. Wrong types are FORMAT, semantic deviations
UNSUPPORTED, with no bitmap, partial IR, color transform or alternative
rendering path. The isolated, non-knockout group is observationally inert
only because all admitted paint remains opaque Normal with no mask.

## Exact opt-in gs

Only public V10 interprets the original `gs` operator with exactly one
name and no pending PATH/Clip definition. Resolve that name from the
effective bounded inherited Page `/Resources /ExtGState`, without
implicit defaults for missing names or unqualified dictionary entries.
Only documented members are admitted: optional `/Type /ExtGState`;
`/CA` and `/ca` numeric **exactly 1.0**; `/BM /Normal`;
`/SMask /None`; `/AIS false`; `/OP false` and `/op false`;
`/OPM 0`. Unknown members, transparency, overprint, multiply,
soft masks, altered rendering intent, external fonts, and other effects
fail closed before any C11 raster work. This is an **opaque GS no-op**,
not general `ExtGState` support.

Tests require the exact unchanged external PDF to render 612×792
**all black RGB8** at 72 DPI, repeat identity/retained owner lifetime,
and strictly rejected synthetic negative group/state variants. The
existing external SHA/license admission still verifies all seven files;
the real opaque-black positive has its own public V10 pixel evidence.
Older V1–V9 Source signatures and the private C11 renderer remain unchanged.

## Integration

Do not modify `tests/external_source_corpus/manifest.json`:
its earlier `supported-path-clip` label is historical metadata, not an
automatic public Native V10 capability. The external-remaining negative
receipt must instead omit this newly admitted original and retain strict
rejection/recovery for the other **three** unqualified originals.
The one original external black pixel-positive is counted once in this
specific CTest, not by a manifest-only `native_conformance_passed_count`.

Keep resource budgets and installed ABI unchanged. Review the parent
Draft dependency stack and exact HEAD CI before any production cutover.
