# Native rectangle and embedded TrueType M1

M0 accepted text-only originals; even a Composer page's painted background
made text unsupported. M1 retains the same Source callback/interpreter,
owned IR and C11 renderer and admits up to four source-ordered opaque
axis-rectangle fills and existing TrueType text shows on full integer,
zero-origin pages at physical 72/96/144 DPI. Original coordinates still
scale by DPI/72, with the same physical fractional fringe intersection.

Before the first BT, the existing Source path interpreter qualifies actual
closed axis rectangles. Those owned commands/colors enter the existing
text interpreter as an ordered prefix. After BT, that same interpreter
handles a single pending re, terminated by f/f* or discarded with n;
rectangle geometry freezes its definition CTM. Positive dimensions and
positive axis CTM are required for this post-BT re subset. Rectangles are
outside BT/ET; a pending re cannot cross unrelated operators. Existing
q/Q, fill color and text/font state remain in that same interpreter.
No original bytes are reparsed and no earlier paint is dropped or treated
as an implied white background.

Owned IR retains PATH rectangles and TEXT_OUTLINES glyph groups in original
order. The existing bridge selects the text/rectangle entry when any text
operation exists, including after a PATH prefix. The same C11 core proves
actual four-corner rectangle topology before sharing its exact nonzero
mask machinery; for one simple rectangle, even-odd and nonzero coincide.
Every glyph still blends separately. Late unsupported paint/state,
malformed input, allocation failure and exhausted budgets discard the
entire output, including earlier backgrounds and text.

The limits remain four original paints in total, 64 source glyphs, 4096
combined commands, 512 admission edges, 1M pixels and 8M aggregate work.
A maximum of three rectangles plus 64 glyphs expand into a bounded borrowed
view; no other Source profile's operation limit changes. All original M0
and TeXample references and external-corpus accounting stay unchanged.
The old late-rectangle refusal is promoted to an independently referenced
positive; Stroke/Image/Clip/nonrectangle and fifth-paint refusals remain.

The offline original-quadratic/Decimal tool adds twelve entire RGBA
references: background/text, text followed by a rectangle, and two opposite
orders of overlapping text/blue fill with a final red composite glyph.
Each case runs at three DPIs. These are generated originals, separate from
the external original-PDF corpus. Public tests exercise real Composer
serialization/reopening, extraction/search, exact RGB, repeat/thumbnail
rendering, owner teardown and same-Context recovery. They reuse the current
public and opaque-state targets; a shared test header avoids duplication
and keeps the existing per-target timeouts.

The current Source/OOM target also tests the four-paint mixed owner graph,
all twelve actual Source C allocation failures, stage-8 C++ failure, four
native allocation failures and complete-pixel recovery, immutable sources,
exact N/N−1 and edge/pixel/operation/physical-plan boundaries. No new CMake,
workflow, consumer/installer project, target, runtime dependency or public
ABI is introduced. Exact-HEAD CI evidence is recorded on the Draft PR.

Images, Clip/Stroke, general post-BT paths, transparency, other fonts and
noninteger/cropped/rotated/UserUnit pages remain outside this increment.
The M0 embedded-font, unhinted outline and text-matrix limits remain in
`native-truetype-text-m0.md`. No shaping, bidi or general PDF parity claim.
