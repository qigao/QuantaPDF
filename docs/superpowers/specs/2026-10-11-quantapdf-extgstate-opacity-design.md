# Graph /ExtGState admission: alpha and named blend modes

Tracks QuantaPDF [#145](https://github.com/qigao/QuantaPDF/issues/145) and
[#154](https://github.com/qigao/QuantaPDF/issues/154), stacked after
Draft #205 → #204 → #203 → #202 → #201 → #200 → #199 → #198.

## Goal

Admit a bounded, provider-independent subset of PDF `/ExtGState` into the
existing immutable Graph Snapshot → private Content IR → Graphics State
→ Composer IR chain. Keep the public C ABI and QPDF Writer unchanged.
Never assume a PDF graphics-state resource is safe simply because its
recognized fields are supported.

## Content grammar and identity

The only newly admitted name operand is `/<name> gs`. A PDF name must be
an exact slash-prefixed 7-bit canonical spelling of at most 127 bytes,
without `#xx` escapes. Escaped names and arbitrary name operators remain
UNSUPPORTED until identity-normalizing name decoding is implemented.

The private operator stores a length and offset into a tape-owned, bounded
byte pool. Resource names are never borrowed from the decoder's temporary
buffer. The aggregate pool size cannot exceed `max_input_bytes`. The
`gs` operator has no numeric arguments. Dangling or misplaced names,
numeric arguments to `gs`, and unsupported operators fail closed.

## Selected Graph resolution

After the entire page's `/Contents` (single stream or ordered array) is
decoded and parsed, the private Graph resolver walks the selected page's
`/Page` → `/Pages` ancestry with a nonzero explicit parent-step budget.
The nearest non-null `/Resources` dictionary wins **as a whole**;
a local empty `/Resources` dictionary prevents accidental inheritance.
Resolve `/ExtGState` and the exact source-name key, then inspect the
dictionary's complete key set.

Supported entries:

| PDF key | Meaning | Internal field |
| --- | --- | --- |
| `/Type /ExtGState` | Optional type tag | Verified, no emitted effect |
| `/ca` | Nonstroking constant alpha | Finite double within [0, 1] |
| `/CA` | Stroking constant alpha | Finite double within [0, 1] |
| `/BM` | Blend-mode name | Normal, Multiply, Screen, Overlay, Darken, Lighten |

Unknown keys (including `/SMask`, `/Font`, `/OP`, `/op`, `/AIS`,
`/TR`, `/LW`, etc.), blend-mode arrays and other supported-PDF-but-
unsupported mode names return UNSUPPORTED. Missing referenced resources,
wrong dictionary/value types or invalid alpha ranges return FORMAT.
No silent partial ExtGState import, resource repair, or fallback to defaults.

Resolve each `gs` into an **operator-indexed delta record**, with explicit
presence bits and a "resolved" marker. A delta containing only `/ca`
does not reset the prior `/CA` or blend mode. No gs operators means no
resource lookups or additional allocations. Changes are temporary owned C
records; no Graph pointers survive the semantic pass. Resource lookup and
decoded stream always use the same immutable selected version.

## Graphics semantics and Writer equivalence

Pure C11 graphics evaluation has a wrapper for the original resource-free
API and a new resolved-delta entrypoint. Missing deltas reject GS-bearing
programs. Initial state is fill alpha=1, stroke alpha=1, blend Normal.
A resource applies only its present values to the current state; `q/Q`
saves/restores alpha and blend along with CTM, clip ID, colors, dash and
line styles.

Each painted path obtains **one** Composer Graphics State resource
containing its effective fill/stroke alpha, blend mode and active clip ID.
The same operation may be translucent and clipped. Default opaque/Normal
paths with no clip create no resource. Converter checks float/four-decimal
roundtrip of both alpha values, supported mode range, source path numeric
precision and the existing transactional budgets before publishing output.
An unsupported state never results in an incompletely rendered path.
`W/W*` still freezes geometry under its own definition-time CTM and
takes effect only after the path's paint/n terminator.

## Verification

Pure C tests verify name operand grammar, owned byte pool, unsupported
name escapes, index-aligned resolver deltas, `q/Q`, partial-field
restoration, invalid alpha ranges, closed failure and Composer resource
deduplication. Native QPDF-generated fixtures place ExtGState under
inherited /Pages /Resources, render translucent overlap, Multiply blend,
Clip+alpha and transparent stroke via the existing PDFium API, and compare
against the detached Composer IR QPDF re-export raster. Repeat with
Rotate=90 and /UserUnit=2.

Additional graph cases prove unknown ExtGState keys reject, unsupported
blend arrays/modes reject, unknown resource names reject and an empty local
/Resources dictionary stops ancestor inheritance. A Graph Commit changing
a referenced /ca numeric value must affect only the descendant Snapshot;
both output IRs remain detached after closing source handles.

## CI gates and next increments

Keep PR Draft and the stacked dependencies intact. Linux Release CTest,
fatal ASan/UBSan, static SDK and Poppler qualification plus macOS and
Windows full CI are expected. Use the existing `full-ci` PR label.

Out of scope: full PDF transparency groups, soft masks, nonseparable blend
modes, overprint, transfer functions, ExtGState stroke override keys, text,
images/XObjects and arbitrary PDF import. Add each only with validated
operator/resource semantics and real source-vs-export raster evidence.
