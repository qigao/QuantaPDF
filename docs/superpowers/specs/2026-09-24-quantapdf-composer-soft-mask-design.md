# Composer Form-backed soft-mask design

Tracks #130 under #128.

## Goal

Add reusable PDF soft masks without introducing raster flattening or a second
compositing backend.

Soft masks reuse existing Composer transparency-group Forms and attach through
the existing immutable graphics-state resource.

## Public API

```c
typedef size_t quantapdf_composer_soft_mask_id;

typedef enum quantapdf_composer_soft_mask_mode {
    QUANTAPDF_COMPOSER_SOFT_MASK_ALPHA = 0,
    QUANTAPDF_COMPOSER_SOFT_MASK_LUMINOSITY = 1
} quantapdf_composer_soft_mask_mode;

typedef struct quantapdf_composer_soft_mask_options {
    size_t struct_size;
    quantapdf_composer_soft_mask_mode mode;
    quantapdf_affine_transform transform;
} quantapdf_composer_soft_mask_options;

quantapdf_composer_add_soft_mask(
    composer,
    form_id,
    options,
    &soft_mask_id);
```

The referenced Form must already exist in the same Composer and must have
`QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP`.

The all-zero transform record means identity. Other transforms must be finite
and non-singular.

## Graphics-state V3

`quantapdf_composer_graphics_state_options` gains one additive V3 tail:

```c
quantapdf_composer_soft_mask_id soft_mask_id;
```

V1 and V2 size macros remain at their previous ABI boundaries.

Effective defaults:

```text
V1: alpha/blend only
V2: alpha/blend + clip
V3: alpha/blend + clip + soft mask
```

soft_mask_id 0 means no `/SMask`. V2 and V3(mask=0) therefore remain
byte-identical.

Graphics-state identity includes the soft-mask ID.

## Soft-mask identity

A soft mask owns no additional variable-size payload. It references existing
Composer-owned Form bytes and stores:

- Form ID;
- Alpha/Luminosity mode;
- normalized affine transform.

Equal tuples deduplicate to one soft-mask ID.

Different modes or transforms receive distinct IDs.

## PDF lowering

PDF soft-mask dictionaries do not carry an affine matrix directly. To preserve
the public displayed-page-space transform contract, each page creates an
isolated wrapper Form whose BBox is the full page:

```text
/SMask <<
  /S /Alpha | /Luminosity
  /G <page-sized isolated transparency Form>
>>
```

The wrapper Form contains exactly one placement of the source Form:

```text
q <displayed-to-PDF matrix> cm /MaskSource Do Q
```

Its resource dictionary privately maps `/MaskSource` to the imported source
Form.

The wrapper uses:

```text
/Group <<
  /S /Transparency
  /CS /DeviceRGB
  /I true
  /K false
>>
```

This makes Luminosity semantics explicit in an sRGB/DeviceRGB group and keeps
Alpha semantics based on the group alpha channel.

No `/BC` is emitted in V1.

## Per-page materialization

A soft-mask resource and graphics-state ID are page-independent public
resources, but their PDF objects cannot always be global because the wrapper's
BBox and displayed-space y conversion depend on page dimensions.

Therefore:

- unmasked ExtGState objects may be shared globally;
- masked ExtGState objects are cached per page;
- soft-mask group wrappers are cached per page and mask ID.

The same public IDs remain valid across pages of different heights.

## Form import

The existing lazy Form importer is reused.

A Form used only by a soft mask is imported when the mask wrapper is built.
It is referenced through the wrapper's private `/XObject` dictionary and is
not added to the page `/XObject` dictionary unless a normal draw_form()
operation also references it.

Referenced Form PDF 1.6 requirements still propagate to the parent document.

## Composition

Soft masks compose with the same graphics-state record as:

- fill/stroke alpha;
- blend mode;
- leaf or compound clip.

The operation-level q/gs/Q isolation from prior phases remains unchanged.

## Validation

Reject:

- null composer/options/output pointer;
- Form ID 0 or unknown Form;
- Form without transparency-group semantics;
- invalid struct size;
- invalid mask mode;
- non-finite or singular explicit transform;
- unknown soft-mask ID in graphics-state V3.

## Qualification

- V2 vs V3(mask=0) byte identity;
- invalid/non-group Form rejection;
- Alpha/Luminosity ID and state deduplication;
- transform-sensitive identity;
- direct `/SMask /S /G` dictionary inspection;
- wrapper Form BBox and isolated-group evidence;
- mask-only Form is not a page XObject;
- nested transparency Form as mask source;
- alpha × outer alpha composition;
- luminosity rendering;
- repeated-finish determinism;
- installed-package compile/link/run;
- Linux/macOS/Windows full CI.
