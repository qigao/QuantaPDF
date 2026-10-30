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

## Work budget: no raised limits

The existing pure C11 V1 PATH sampler conservatively charged all four
edges at each of the 16 antialias samples. The original 612×792 full-page
black rectangle therefore required 31,021,056 edge checks and refused the
public 8,000,000 maximum even after correct Source admission.

The **same** C11 raster now recognizes a full-device rectangle only from
its actual flattened, exactly connected, four distinct axis-aligned
corner edges with no Clip and normal opaque fill. The geometry proves
that all 16 samples of every device pixel lie inside under both
nonzero and even-odd winding. It uses the existing RGBA blending code
with 16/16 coverage, charging the unchanged limit a conservative **one
coverage decision per original sample**: 612×792×16 = 7,755,264.

Any partial rectangle, diagonal/self-crossing path, Clip, generic curve
or Stroke retains full generic edge work accounting. A one-less
max_sample_tests still fails atomically; even a larger true full-page
rectangle still refuses if 16×pixels exceeds 8,000,000. A standalone
pure-C11 CTest verifies the exact black pixels, winding parity, repeated
pixels and every negative limit/geometry case. There is no second raster
or provider retry.

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
