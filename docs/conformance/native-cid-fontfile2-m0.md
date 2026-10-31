# CID TrueType FontFile2 export packaging — M0

The existing SFNT subset writer distinguishes standalone Unicode programs
from PDF CIDFontType2 FontFile2 programs. The default Unicode path retains
its cmap. Composer embedded-text and glyph-run exports explicitly request a
CID program, whose character selection comes from PDF CIDToGIDMap instead.
ISO 32000-1 section 9.9.2 requires cmap omission for this FontFile2 use.
This change does not remove cmap from CFF/OpenType FontFile3 resources.

Both Composer routes always use the successful CID subset, even when its
size is not smaller than the original. Subset failure propagates its status
and publishes no Output; copying the original font would reintroduce the
invalid packaging. Existing CID/GID identities, widths, component closure,
ToUnicode and decoded Length1 remain unchanged in meaning.

## Acceptance in existing native targets

- Source/OOM exercises the actual shared writer: default/explicit Unicode
  cmap and glyph mapping, cmap-free CID directory/search fields, every table
  checksum and the whole-font checksum, required tables, .notdef and accented
  composite outline/metric identity, invalid purpose/GID, one real allocation
  failure and successful recovery.
- The public mixed-text target inspects actual Composer embedded-text and
  glyph-run serialized PDFs: Type0/CIDFontType2, CIDToGIDMap identity/stream,
  ToUnicode, cmap absence, decoded Length1 and whole-font checksum.
- Existing Hello/background references remain unchanged. An additional H
  glyph-run is reopened, extracted/searched and rendered at physical
  72/96/144 DPI, with repeats, thumbnails and detached lifetimes. Its complete
  expected image uses the independently authored H part of the existing
  Hello/background reference plus gray elsewhere: the original H right edge
  is before x=29pt and the following e starts after x=29pt.
- Cyclic composite glyphs must fail Composer finish with FORMAT/NULL Output
  on both embedded-text and glyph-run routes. Previously subset rejection
  could be hidden by copying the original program.

No public ABI, native paint/glyph/edge/work quotas, original external corpus,
reference image, CMake target or workflow changes are needed. New acceptance
belongs to existing Source/OOM and public mixed-text tests. Exact-head CI
qualification is recorded in the associated PR; this document alone is not
runtime acceptance.

Design: [#570](https://github.com/qigao/QuantaPDF/issues/570).
Font coverage: [#145](https://github.com/qigao/QuantaPDF/issues/145).
