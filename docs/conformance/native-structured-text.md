# Native structured text and search

`quantapdf_extract_structured_text` now shares the bounded source-font,
ToUnicode, text-state and Form interpreter used by
[plain text](native-text.md). It returns the existing detached text snapshot;
search uses that snapshot alone. Neither operation opens PDFium. The old
PDFium structured-text entry point, character collector and text-page calls
are deleted. Rendering remains the production PDFium caller.

## Geometry and style contract

Each source glyph receives a **loose selection cell** whose horizontal extent
is its PDF advance width and whose vertical extent is the font's typographic
ascent/descent. Embedded simple and horizontal Type0 fonts use their
FontDescriptor Ascent/Descent in 1000-em units. The twelve admitted Core14
Latin faces can use the existing Adobe AFM metrics when no descriptor is
present. Missing descriptor metrics on other fonts are unsupported; invalid
or non-finite metrics fail explicitly. No glyph outline is decoded by this
observation, and these cells are not glyph ink bounds or a font-rasterization
qualification. Zero-advance glyphs retain zero-width cells.

The text matrix, horizontal scale, rise, CTM, Form matrix, visible page box,
quarter rotation and UserUnit transform all four cell corners. Character
quads preserve that orientation; span/line/block bounds enclose their corners.
Line direction follows the transformed baseline in public display coordinates.
Span font size is the transformed vertical em size, including UserUnit once.
This makes rotation, nonuniform scaling and shear explicit rather than using
an untransformed font-size field as a device-space measurement.

Every Unicode scalar mapped from one source glyph shares that complete cell;
a ligature or combining cluster advances only once per source glyph. An
unmapped source code follows the plain-text U+FFFD rule. Search can cross
style spans on a line, but not a line boundary. Its existing projected quad
construction encloses the matched cells in the line's orientation. Generated
spaces occupy the gap between adjacent cells and retain the preceding style.

Output remains in source operator order, using the documented plain-text
baseline/gap inference. Newlines become line boundaries instead of searchable
characters. The current grouping creates one block with ordered lines and
style spans; it does not reconstruct semantic paragraphs, columns or bidi.
Bidi levels and flags remain zero. Reading-order reconstruction, vertical
writing, ActualText, shaping and outline-specific geometry remain follow-up.

Span ARGB represents the source nonstroking color and constant alpha, not the
final composited pixel color. DeviceGray/DeviceRGB, named resources resolving
to them, g/rg/cs/sc/scn and ExtGState ca are admitted, with q/Q restoration.
Other text fill spaces, including CMYK/ICC/pattern color, are explicit
unsupported cases when text is emitted. A later supported color can replace
an unused unsupported state. Stroke-only text still reports the nonstroking
style, as the former fill-color observer did. Clip, transfer functions and
spatial soft masks are not pixel-sampled by this observation.

## Bounds and ownership

All existing plain-text resource/content/work limits remain in force. A
structured projection additionally caps Unicode records at 262,144 and the
published snapshot at 64 MiB, including strings and index tables. Coordinate
magnitudes and effective em sizes must be finite and below FLT_MAX/8, keeping
subsequent search projections bounded. A zero or singular text basis is
unsupported. Plain text does not acquire these geometry/color restrictions.

No snapshot is published until complete content admission and allocation
succeed. Every failure resets the output. Text bytes, characters, styles,
indices and geometry are copied; the snapshot retains only its existing
context owner, never a source page, QPDF object or renderer handle.

## Evidence

The production-source no-PDFium executable includes independent numeric
cell/search goldens for all four page rotations with CropBox/UserUnit,
encrypted input, inherited/local Form resources, affine/sheared placement,
style/alpha restoration, cross-span search, generated spaces, multi-scalar
ToUnicode mappings and descriptor metrics for CIDFontType0/2. It checks empty
snapshots, detached lifetimes, missing/invalid metrics, unsupported text colors,
late unsupported content and record-cap exhaustion with atomic output reset.
Existing structured/search, context, crop/trim, security and recompression
suites use the same native path. Composer TTF, CFF/CFF2 and glyph-run tests
also validate native structured extraction and Unicode search.
