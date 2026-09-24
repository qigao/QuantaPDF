# Composer SVG V2B2 symbol/use Form design

Tracks #113 under #110 / #104.

## Goal

Add exact local reusable SVG symbols without expanding repeated symbol geometry
into the parent PATH operation list.

A referenced symbol lowers once to an immutable Composer Form resource.
Subsequent use instances lower to Composer FORM operations.

## V1 syntax

Definitions:

```xml
<defs>
  <symbol id="..." viewBox="minx miny width height"
          preserveAspectRatio="...">
    ...
  </symbol>
</defs>
```

Instances:

```xml
<use href="#symbol-id"
     x="..." y="..."
     width="..." height="..."
     transform="..."/>
```

Only fragment-local href targets are accepted. xlink:href and external URLs are
rejected in V1.

A use target must be a symbol definition. Referencing normal rendered elements,
gradient IDs, or clip IDs as use targets is rejected.

## Immutable instance style boundary

V1 use does not accept presentation-style overrides.

This is intentional: one symbol must map to one immutable Form snapshot for
true reuse. Allowing fill/stroke/opacity inheritance to vary at each use site
would require a distinct Form cache key per normalized inherited style.

A later extension may cache by (symbol ID, inherited style), but V1 accepts only
placement attributes on use.

The symbol content owns its own styles.

## Definition representation

V2B2 extends the B1 side-effect-free definition pre-pass with:

```text
symbol_definition
  id
  viewBox
  preserveAspectRatio
  token subtree
  dependency symbol IDs
```

The subtree is represented by copied parsed element tokens rather than raw
source pointers. No caller/SVG input pointer survives parsing.

B1 gradient and clip definitions remain in the same immutable definition table.

## Dependency graph

Nested symbol use is allowed.

Before Composer publication, run DFS over symbol dependencies:

```text
unseen -> visiting -> done
```

Encountering a visiting node is a cycle and fails with UNSUPPORTED.

Unresolved and wrong-kind use targets also fail before publication.

## Form materialization

A symbol is materialized lazily on first referenced use.

The symbol's local Form dimensions are the viewBox width/height. The builder
renders the symbol subtree into a temporary child Composer using the existing
SVG staging/lowering helpers and B1 definitions.

The builder must not call a new SVG/qpdf backend. It publishes through the
existing Composer primitives:

- PATH / dash;
- graphics state / opacity;
- gradient paint;
- clip path;
- nested Form resources.

The Form local root transform translates the symbol viewBox minimum to local
(0,0). Internal preserveAspectRatio only matters when a symbol establishes a
nested viewport; the use placement controls how the completed local Form maps
into its requested width/height.

## Use placement

Let symbol local dimensions be W x H and use destination width/height be Dw x
Dh.

The use preserves the symbol's preserveAspectRatio setting:

- none -> independent x/y scale;
- meet -> uniform min(Dw/W, Dh/H) + alignment;
- slice -> uniform max(...) + alignment plus a destination clip.

The resulting displayed-space affine transform is composed with:

1. use x/y translation;
2. preserveAspectRatio viewport mapping;
3. use transform;
4. the SVG ancestor transform.

That transform is passed to quantapdf_composer_draw_form().

For slice, the instance also needs an outer Composer clip state. Because
draw_form() already accepts graphics_state_id, slice can reuse the existing
graphics-state/clip primitive without changing Form ABI.

## Transactional publication

SVG parsing and dependency resolution have no Composer side effects.

Before publication snapshot:

- form count;
- paint count;
- clip count;
- graphics-state count;
- operation count;
- resource bytes.

Forms are materialized recursively in dependency order. Each form builder works
inside its own temporary child Composer. Parent FORM operations are staged only
after all referenced forms and per-instance slice clips/states succeed.

On failure:

- drop/free all newly registered parent forms;
- free new paint/clip payloads if any;
- restore state/resource/operation snapshots;
- publish nothing from the failed SVG draw.

## Security boundary

No I/O is introduced.

Rejected:

- external href;
- xlink:href;
- scripts;
- browser CSS;
- arbitrary use targets;
- duplicate IDs;
- unresolved IDs;
- cyclic symbol dependencies;
- presentation attributes on use;
- unsupported symbol children;
- event attributes.

## Qualification

- forward/backward symbol references;
- repeated use installs one Form resource;
- different x/y/width/height/transform placements;
- none / meet / slice placement;
- nested acyclic symbols;
- cycle/unresolved/wrong-kind rejection;
- symbol content using B1 gradient and clip resources;
- direct /Subtype /Form evidence;
- PDFium render and text extraction where applicable;
- deterministic repeated finish;
- transaction rollback;
- Windows installed-package smoke;
- full Linux/macOS/Windows CI.
