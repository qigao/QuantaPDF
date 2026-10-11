# PDF page /Contents and graphics semantics (restricted path IR)

Continuation of [#145](https://github.com/qigao/QuantaPDF/issues/145), stacked on #199 / #198.

## Admission and ownership

Use a selected immutable Graph Snapshot and a zero-based page index. Look up the
page's direct `/Contents` member: missing/null means empty content, a stream is
one content section, and an array is an ordered sequence of stream references.
Every array member is validated as a stream before replay. Wrong types fail
FORMAT. Each stream is decoded via #198's version-checked bounded decoder,
with a maximum stream count, independent per-stream decode/replay limits, and
a shared total decoded-content byte budget. Join with exactly one newline
between elements, preserving operands, comments, graphics state and path state
across boundaries. This is the conventional content concatenation model.

Only after the entire array has been decoded and the whole content stream
has passed syntactic and semantic validation are both the typed tape and
the graphics semantics atomically published. The result is malloc-owned and
detached from the source document, parser, graph, context and temporary bytes.
Graph lineage/origin and provider decoding policy stay unchanged.

## Graphics state machine

Pure C11 semantic evaluation consumes #199's admitted path-operator tape.

- CTM begins as identity relative to default page user space; `cm`
  premultiplies its six-parameter PDF row-vector matrix (new * old).
  Nonfinite arithmetic is rejected, not saturated.
- Color defaults to DeviceGray black for fill and stroke and is represented
  internally as normalized RGB. Width defaults to 1. Colors outside [0,1]
  and negative widths are rejected.
- `q` saves the complete graphics state, including the active clip-chain
  identity; `Q` restores it. Current paths are not part of the saved state.
- A path's construction operations, paint terminal, CTM, color and stroke
  parameters remain in typed private data; no rendering or coordinate
  normalization is implied.
- `W/W*` records deferred clipping intent. The clipping chain is extended
  **after** the path terminator, as a persistent parent-linked record.
  A `Q` restores the prior active clip identifier. The chain references
  source path operator spans; clipping polygons are **not** intersected.
- A path must be completed by an applicable terminal; other graphics-state
  operators in the middle of a path, repeated `W/W*`, unsupported operators
  and unclosed q/Q blocks fail closed. No partial result is published.

This does **not** convert arbitrary PDF pages. Text, fonts, images, XObjects,
pattern/shading resources, transparency, complex colors, dash patterns,
and unknown PDF operators remain unsupported by #199's restricted grammar.
Likewise `/Rotate`, `/UserUnit`, inherited resources and page boxes are not
applied here. A *geometrically clipped and renderer-equivalent* Composer IR
conversion is a separate required step, not claimed by these semantics.

## Verification

New independent C11 tests validate noncommutative CTM composition, q/Q,
colors/line width, delayed W/W* activation, nested clips and restores,
invalid graphics objects and overflow rejection. Snapshot integration changes
the selected page's original text stream into a two-element content array:
`q`/CTM and an incomplete operand sequence begin in the first stream; the
second supplies its remaining operand, paints the path, and restores state.
Tests assert decoding through selected graph versions, exact ordering,
aggregate size and stream-count budgets, output reset, and output lifetime
after graph/context destruction. The existing full CTest/SDK suite remains
the merge gate.

## Next increment

Lower only admitted, geometrically representable path + graphics operations
into Composer IR under explicit float/CTM/clip rules. Unsupported renderer
semantics must return UNSUPPORTED, not silently lose a clip or change stroke
geometry. Retain Draft status until Linux static/ASan/UBSan and Windows
qualification are complete.
