# PDF Clip geometry -> Composer Clip resources (first delivery)

Continuation of [#145](https://github.com/qigao/QuantaPDF/issues/145)
and [#154](https://github.com/qigao/QuantaPDF/issues/154),
stacked on Draft #203 → #202 → #201 → #200 → #199 → #198.

## Boundary

The PDF operator interpreter added by #200 already creates an immutable
path/graphics tape with W/W* clip records; each clip definition is activated
**only at its path terminator**, with a permanent parent clip ID.
Graphics-state q/Q restores the previous clip identity. The #203 path
lowering previously rejected any nonzero clip identity.

This increment lowers that recorded geometry into the **existing** Composer
Clip Resource Registry, without changing the public API, Composer writer,
or PDF provider:
- A W/W* path becomes an owned CLIP_PATH (M/L/C/h commands and fill rule).
- Its transform is the **definition-time** content CTM followed by the
  selected page affine display map (same matrix-conjugation logic as the
  corresponding painted path). Future cm operations do not move old clips.
- A descendant clipping state is represented by a CLIP_INTERSECTION
  containing each unique recorded ancestor leaf. Its exact semantics is
  iterative PDF clipping (logical geometric intersection), including W*.
- A painted path references the effective **previous** clip through a
  Composer graphics-state resource. If the same source path also contains
  W/W*, its painting is lowered first, and the new clip becomes effective
  only for later paths. This preserves stroke pixels outside a newly
  declared clipping boundary.
- A W/W* ... n path emits a Clip resource but does not emit a spurious
  drawing operation. A naked n without clipping still fails closed.
- q/Q references the immutable clip ID present at each painted operation;
  restoring Q never reuses a newer clip inadvertently.

Clip chains are bounded to QUANTAPDF_COMPOSER_MAX_CLIP_COMPONENTS (64)
per admitted content sequence. Painted-operation and *aggregate* path
command limits remain caller supplied; clip commands count against the
same command budget. Candidate clip/graphics resources use the existing
uniquely-owned interning protocol. The entire output remains transactional:
a failed path, unsupported geometry, numeric precision loss, clip depth,
budget exhaustion or allocation failure drops all partial IR resources
and publishes a zeroed output.

## Rendering and precision

The private Composer writer already handles CLIP_PATH and
CLIP_INTERSECTION by replaying path contours as W/W* n before each
painted operation within a local q/Q pair. Its Clip transform operates
on Composer Y-down path coordinates and writes PDF Y-up commands.
The lowering uses #201's strict float/four-decimal numeric projection for
both painted paths and clip definition-time CTMs, and #203's verified page
transform for crop offsets, page /Rotate and /UserUnit.

No raster masks, polygon clipping approximations, changed Writer policy,
vendor dependencies, compatibility paths, or public C ABI additions.

## Tests and qualification

Add a provider-independent C11 test to verify:
- Nested W and W*, with even-odd holes and interning into path + intersection
  resources, correct q/Q restoration and scoped graphics-state IDs.
- W with simultaneous S: the stroke must be painted under the old clip.
- Clip CTM frozen at W even if later cm restores source drawing coordinates.
- Clip-only n records, detached lifetime after source tape destruction,
  failed publication under aggregate budget or precision mismatch, and
  hard failure at 65 clip definitions.

Add QPDF/PDFium integration tests. The source PDF is independently rendered
through the public QuantaPDF/PDFium SDK. The committed immutable Graph Snapshot
is lowered to Composer IR; source handles are closed/dropped. QPDF exports
the detached IR twice deterministically, and PDFium renders it. Compare
raster dimensions and pixels for nested q/Q, W* holes, delayed W+S stroke
and a clip defined under a translated CTM on a rotated /UserUnit=2 page.

The full Linux Release, fatal ASan/UBSan, installed SDK and Poppler gates
remain required. Windows/macOS qualification is tracked separately.

## Remaining limitations

Only the operator vocabulary admitted in #199/#200 is supported.
External graphics state, text/images, transparency, dash/miter/cap changes,
patterns, compositing and arbitrary PDF imports remain unsupported.
Numeric values not representable by Composer's float/decimal and RGB8
encoding reject before publication. Page CropBox outside MediaBox remains
an explicit unsupported policy until its intersection contract is proven.
This increment establishes an exact, bounded Clip *resource* representation
for the already admitted path subset, not general PDF raster equivalence.
