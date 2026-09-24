# Composer Form V2 transparency-group design

Tracks #117 under #116.

## Goal

Allow reusable Form resources to opt into native PDF transparency-group
semantics so an outer graphics-state opacity or blend mode applies to the
completed Form as one composited group.

Without a transparency group, an outer alpha value participates in each child
painting operation inside the Form. Overlapping children therefore accumulate
alpha independently. A transparency group first composites the Form's children
and then composites the resulting group into the parent.

## Public ABI

`quantapdf_composer_form_options` gains an additive V2 tail:

```c
#define QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP 0x1
#define QUANTAPDF_COMPOSER_FORM_FLAG_ISOLATED          0x2
#define QUANTAPDF_COMPOSER_FORM_FLAG_KNOCKOUT          0x4

typedef struct quantapdf_composer_form_options {
    size_t struct_size;
    float width_points;
    float height_points;
    uint32_t flags; // V2
} quantapdf_composer_form_options;
```

The V1 SIZE macro remains the pre-2.25 boundary at the offset of `flags`.
V1 callers therefore imply flags=0 and remain ordinary Forms.

V2 validation rules:

- unknown bits are rejected;
- isolated/knockout require transparency-group;
- transparency-group alone is valid;
- isolated and knockout may be independently enabled once group semantics are
  enabled.

## Ownership and deduplication

Flags are normalized at registration and stored in the parent Form resource.
The existing deterministic child-PDF snapshot remains the content identity.

Form deduplication key becomes:

```text
(width, height, flags, serialized child PDF bytes)
```

Forms with identical content but different group semantics therefore receive
different IDs.

Flags do not change child Composer construction or resource-budget accounting:
the parent still owns only the serialized child PDF bytes.

## PDF lowering

After qpdf converts/imports the stored page snapshot into a Form XObject, a
flagged Form receives:

```text
/Group <<
  /S /Transparency
  /CS /DeviceRGB
  /I true|false
  /K true|false
>>
```

V1/flags=0 Forms receive no /Group entry.

The existing `draw_form()` placement and graphics-state attachment are
unchanged. Outer `ca`, `CA`, and `BM` therefore apply naturally to the
grouped Form.

PDF 1.4 already defines transparency groups, so no version increase beyond the
existing minimum is required.

## Nested Forms

A transparency Form may contain ordinary or transparency Forms. Nested group
dictionaries are preserved inside child snapshots by the existing Form import
pipeline.

## Qualification

- V1 vs V2(flags=0) byte identity;
- unknown/invalid flag combinations rejected;
- exact content + flags deduplication;
- direct /Group /S /CS /I /K inspection;
- ordinary Form has no /Group;
- half-alpha overlapping children demonstrate group-level compositing;
- isolated group rendering;
- knockout dictionary qualification;
- nested transparency Forms;
- deterministic finish;
- installed-package V2 Form smoke;
- Linux/macOS/Windows full CI.
