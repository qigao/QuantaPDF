# Composer reusable Form XObject design

Tracks #103 under #99.

## Public model

V1 exposes a synchronous builder callback rather than a second family of
form-specific draw APIs.

```c
typedef size_t quantapdf_composer_form_id;

typedef struct quantapdf_composer_form_options {
    size_t struct_size;
    float width_points;
    float height_points;
} quantapdf_composer_form_options;

typedef quantapdf_status (*quantapdf_composer_form_builder_fn)(
    quantapdf_composer *form_composer,
    size_t page_index,
    void *user_data);

quantapdf_composer_add_form(
    parent,
    options,
    builder,
    user_data,
    &form_id);
```

The callback receives a temporary child Composer with exactly one pre-created
page at index 0. It can use the existing text/font/image/path/paint/graphics
state/clip/form APIs.

Navigation and outlines are not valid Form content.

## Snapshot and ownership

The child page has a private transparent-background flag. On successful
callback completion:

1. require the single pre-created page and no navigation/outlines;
2. deterministically compose the child to an owned one-page PDF;
3. drop the temporary child Composer;
4. deduplicate forms by dimensions + exact serialized bytes;
5. charge the stored PDF bytes to the parent resource budget.

No callback pointer, user pointer, or child Composer survives registration.

Nested forms are naturally acyclic: a form can only contain already-completed
form snapshots owned by its temporary child Composer.

## Placement

```c
typedef struct quantapdf_composer_form_draw_options {
    size_t struct_size;
    quantapdf_composer_graphics_state_id graphics_state_id;
} quantapdf_composer_form_draw_options;

quantapdf_composer_draw_form(
    composer,
    page_index,
    form_id,
    transform,
    options);
```

The all-zero transform record means identity. Form local displayed coordinates
are [0,width] x [0,height].

For form height H, public displayed transform:

```text
X = a*x + c*y + e
Y = b*x + d*y + f
```

lowers from the Form XObject's native PDF y-up coordinates to the parent PDF as:

```text
[a, -b, -c, d,
 c*H + e,
 page_height - d*H - f]
```

Identity therefore places the form's top-left at the parent page top-left.

The common operation graphics-state attachment can wrap the Form placement, so
opacity/blend/clip apply to the whole reusable fragment.

## Backend reuse

Registration serializes the temporary child with the existing single qpdf
backend. Parent finish parses the stored one-page PDF and uses:

```text
QPDFPageObjectHelper::getFormXObjectForPage()
QPDF::copyForeignObject()
```

The imported Form therefore retains child fonts, images, paint patterns,
ExtGState resources, nested forms, and ToUnicode mappings without a second
resource assembler.

If a stored form snapshot requires PDF 1.6 (for example embedded CFF/CFF2), the
parent output is also raised to at least PDF 1.6.

## Qualification

- callback/error/size validation;
- transparent local page;
- exact-byte form deduplication;
- parent resource-budget failure;
- repeated placement with translation/rotation/scale;
- text extraction from Form content;
- image/path/gradient/clip resources inside forms;
- outer opacity/clip graphics state around Form placement;
- nested forms;
- direct /Subtype /Form and BBox inspection;
- repeated-finish determinism;
- Windows installed-package consumer.
