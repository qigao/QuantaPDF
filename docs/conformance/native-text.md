# Native plain-text observations

`quantapdf_extract_text` uses bounded source-content interpretation and the
structure provider. The plain-text path does not open or call PDFium, and its
former PDFium entry point is deleted. The C11 signature and caller-owned,
NUL-terminated UTF-8 allocation remain unchanged. An empty page returns an
allocated empty string. Returned bytes survive page/document destruction.

## Admitted interpretation

- Type1 and TrueType simple fonts, direct or indirect resource dictionaries,
  defined WinAnsi characters and the printable ASCII part of StandardEncoding
  (including its curly quote positions). Symbol/ZapfDingbats require ToUnicode.
  Extended StandardEncoding and Differences without an authoritative mapping
  remain unsupported when those character codes are used.
- ToUnicode with uniform one- or two-byte codes, codespacerange, bfchar and
  bfrange, including array destinations, UTF-16BE surrogate pairs and mappings
  containing multiple Unicode scalars. Source codes outside the declared code
  space are malformed. An unmapped code inside that space produces U+FFFD;
  this also preserves Composer clusters whose continuation glyph has no
  separate mapping. It never guesses Unicode from a CID or glyph ID.
- Horizontal Type0 Identity-H fonts with CIDFontType0/CIDFontType2 descendants,
  authoritative ToUnicode and DW/W advances. Simple font Widths/MissingWidth
  and the existing Core14 AFM widths drive source-glyph advances. A ligature's
  full Unicode sequence is emitted together, with one advance per source glyph.
- BT/ET, Tf, Tm, Td/TD/T*, Tj/TJ, quote operators, character/word spacing,
  horizontal scale, rise and leading. q/Q restores text parameters and CTM,
  while text/line matrices retain their PDF text-object lifetime.
- Multiple content streams, inherited page resources, nested Form XObjects,
  local font resources and Form matrices. ExtGState Font is supported.
  Image XObjects are ignored by this observation; image decoding is separate.
  Parser streams belong to a temporary QPDF, never the source document.

Output follows source operator order. Across emitted source glyphs, a baseline
change exceeding half the larger font height (minimum 0.1 source units), or
a direction change with dot product below 0.95, inserts CRLF. A forward gap
exceeding one fifth of that height (minimum 0.1 units) inserts a space unless
one is already present. This is deterministic plain-text whitespace inference,
not reading-order reconstruction or byte-for-byte PDFium whitespace emulation.
Page rotation/UserUnit do not change text order. Structured text, search and
character geometry still use the legacy provider; this increment does not
claim native glyph bounds, bidi, shaping or vertical writing.

## Refusal and work bounds

Unsupported font types, non-Identity-H composite encodings, mixed/long CMap
codes, usecmap/UseCMap, vertical writing, executable CMap control flow, inline
images, ActualText replacement spans and unknown content operators fail
explicitly. Invalid UTF-16, NUL mappings, duplicate mappings, overlapping code
spaces, malformed text/graphics state and cyclic Forms are rejected. Neither
failure nor capacity exhaustion returns a partial string or invokes PDFium.

Each extraction allows 8 MiB of cumulative decoded content (including repeated
Forms and stream separators), 8 MiB of output UTF-8, 8 MiB of aggregate font
CMap inputs, 1 MiB per CMap, 256 fonts and 131,072 retained mapping/width entries.
Each CMap permits 65,536 mappings, 256 code-space ranges, 200,000 outer lexer
visits and 32 UTF-16 code units per destination. Mapping-section expansions
are separately bounded by the mapping count. Content permits 1,000,000
operator/operand/glyph visits, 1,024 streams per content list, 100,000 TJ array
items, 32 Form levels, 128 graphics saves/marked-content levels and 16 filters.
A singular text baseline is rejected. These are projection limits, not quotas
for every allocation internal to qpdf. Lossless decoding is capacity checked
while streaming, before appending decoder output.

## Verification

The production-source `quantapdf.native_document` executable links no PDFium
and retains aborting tripwires for the remaining legacy operations. Independent
text goldens cover WinAnsi, encrypted inputs, one/two-byte CMaps, bfrange scalar
and array forms, non-BMP characters, ligatures/combining sequences, missing
mappings, q/Q semantics, TJ/quote spacing, explicit glyph positioning, Forms,
inherited/local resources, gs Font, detached lifetimes and atomic refusals.
Existing Composer TTF/CFF/glyph-run, affine/Form, rewrite/security and crop/trim
suites exercise the same public plain-text path.
