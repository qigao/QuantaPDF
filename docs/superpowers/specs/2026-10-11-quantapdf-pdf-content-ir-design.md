# PDF content path operators to private IR (first increment)

Continuation of #145 on top of #198, using #146/#159 stable graph identity.

## Boundary

The selected immutable graph owns encoded bytes. The private #198 snapshot stream
operation produces detached, bounded non-lossy decoded bytes. A pure C11 parser
consumes them synchronously and emits an owned typed operator tape, published only
after full admission. Neither source parser, selected graph, decoded buffer nor
context is retained.

This is not a general PDF content interpreter or Composer lowering. The tape
retains argument order and decoded-stream byte offsets in PDF user space. It
does not resolve font/image resources, patterns, shadings, forms, transparency,
color spaces or XObjects; it does not rasterize or compute drawing bounds.

## Restricted vocabulary

- Graphics: q, Q, cm, w, G, g, RG, rg.
- Path construction: m, l, c, v, y, h, re.
- Painting: S, s, f/F, f*, B, B*, b, b*, n.
- Clipping intent: W and W* (record only, do not apply).
- Whitespace, comments and finite decimal PDF numbers.

Any other PDF operator or value token (BT/ET, text strings, names, arrays,
dictionaries, inline images, Do, dash arrays or resource references) is
UNSUPPORTED. Invalid operand counts, malformed numbers, path prerequisites,
negative line width and mismatched q/Q are FORMAT. No partial tape escapes.
Empty streams produce an owned empty tape.

A single complete stream is currently parsed. Page /Contents arrays, including
q/Q or operands crossing stream boundaries, are intentionally out of scope.
Such contents must not be treated as a successful full-page interpretation.

## Limits and ownership

The caller supplies nonzero max_input_bytes, max_operators and
max_graphics_depth. Stream decoding has its own max_decoded_bytes, graph replay
depth and expanded-visit budgets. The C parser owns at most admitted operation
records with six doubles per record and scans input without recursive descent.
The upstream QPDF replay and codec workspace are not bounded by parser budgets.
Neither public C ABI nor installed SDK symbols/dependencies change.

## Verification

A standalone pure C test checks operation typing, ordering, numeric operands,
comments, unsupported text/image/resource syntax, invalid arity/state, budget
exhaustion, output reset and detached lifetime. A snapshot test replaces a text
content stream in a committed descendant with path content; it verifies stable
reference resolution, version-specific decoding, atomic failure on small limits,
and tape lifetime after the source graph and context have been dropped.

## Follow-up

Implement /Contents array traversal as one logical content sequence, graphics
state evaluation and resources, then a tested lowering to the existing Composer
IR. Direct rendering, image/font mapping and arbitrary PDF-to-IR import remain
future work under #145/#154.
