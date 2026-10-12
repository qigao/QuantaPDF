# PDF stroke style operators (J / j / M / d) → Composer IR

This increment advances QuantaPDF architecture #145 and #154, stacked on
Draft #204 → #203 → #202 → #201 → #200 → #199 → #198.

## Scope and invariant

The private PDF Content IR currently admits paths and W/W* clipping but not
the PDF stroke graphics parameters beyond `w`. Composer Path options and
Dash Pattern already support cap, join, miter limit, dash array and phase;
do not modify the public API, QPDF Writer or backend provider.

Extend the **private** PDF operator vocabulary with:

| PDF | Meaning | Stored representation |
| --- | --- | --- |
| `J` | Line cap | 0 butt, 1 round, 2 projecting square |
| `j` | Line join | 0 miter, 1 round, 2 bevel |
| `M` | Miter limit | finite real ≥ 1, default 10 |
| `[a... ] phase d` | Dash pattern | nonnegative array values + phase |

The dash array is the **only** newly admitted PDF array syntax. Its length
is at most 64, matching Composer's MAX_DASH_COUNT. Reject nested arrays,
strings, names, dictionaries, unsupported operands and unterminated arrays;
require a `d` immediately after the complete array and one numeric phase.
The dash phase must be finite/nonnegative. At least one array entry must be
positive unless the array is empty (which means a solid line and ignores phase).
PDF comments and compact token separators are legal inside the array.

Operator records contain offsets/counts into a growable **tape-owned numeric
pool**; there are no pointers into decoded PDF source bytes. The cumulative
number of stored dash values is bounded by the explicit input-byte limit;
individual array size is capped at 64. Allocation failures and budget
overflow publish no partial tape. Graph snapshots, source documents and
their contexts may be destroyed after parsing.

## Graphics State and lowering

A graphics state includes line width, cap, join, miter limit and a borrowed-by-
offset dash selection. `q` pushes this state and `Q` restores the exact
previous values, alongside current CTM, colors and active Clip identity.
The current path is not a graphics state member.

Each painted **stroke** operation lowers the selected style into existing
Composer Path options: cap/join enum, float miter, owned float dash lengths,
float phase and float width. Fill-only operations do not need a dash payload.
A reset `[] 0 d` represents solid stroke; writer leaves the operator out.

Strict number admission from #201 remains: if source miter, dash length,
phase or width would lose representable four-decimal precision or float range,
return UNSUPPORTED, **never silently round or clamp**. Numeric values that
contradict the PDF grammar fail FORMAT. Path-dash owned allocation is
released automatically by `quantapdf_ir_dispose`; all failure paths remain
transactional. Clip geometry keeps definition-time CTM mapping and is
independent of stroke cap/join/dash/miter changes.

Source PDF graphics state and Writer-emitted Composer graphics state remain
isolated by explicit q/Q scopes. A later drawing operation must not inherit
the wrong dash array or cap when a saved state is restored.

## Qualification

New pure C11 parser tests validate numeric arrays with comments, tight
delimiters, operand grammar, cap/join enum restrictions, negative or all-zero
dash patterns, 65-element budget overflow and empty reset.
Graphics-state tests verify q/Q restoration, including selected dash pool
offset and phase. Composer lowering tests verify owned dash lifetime after
source destruction, cap/join/miter projection and failure on values requiring
more than four decimal places.

The native PDFium comparison extends the existing QPDF → selected Graph
Snapshot → Composer IR → QPDF pipeline with square and round line caps,
round and bevel joins, miter control, nonzero dash phases, an empty dash
reset, clipped stroke, q/Q restoration and a rotated /UserUnit=2 page.
Comparison includes dimensions, interior/exterior representative colors and
full-frame RGB differences.

## Acceptance

Run full Linux Release (CTest), fatal ASan/UBSan, installed SDK and Poppler
qualification. Request the PR's `full-ci` label for macOS/Windows checks
when feasible. Keep all stacked branches Draft and do not merge before the
relevant platform checks. No public ABI or external dependency changes.

Remaining: richer line rendering operators, precision-independent color and
paint resources, arbitrary PDF content, Clip outside MediaBox and general
backend-independent rendering. These remain open architecture work.
