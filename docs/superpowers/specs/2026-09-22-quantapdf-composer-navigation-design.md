# QuantaPDF Composer Navigation Design

**Date:** 2026-09-22  
**Status:** Initial implementation for #71

## Goal

Add generated navigation to Composer without exposing qpdf/PDFium objects or raw
PDF action/destination dictionaries.

The public model covers:

- URI link annotations;
- internal page link annotations with explicit page-space target points;
- hierarchical internal outlines/bookmarks.

Generated output must round-trip through the existing QuantaPDF read APIs:
`quantapdf_extract_links()` and `quantapdf_document_outline()`.

## Public contracts

### Links

`quantapdf_composer_add_uri_link()` binds a displayed-page-space hotspot to a
copied UTF-8 URI.

`quantapdf_composer_add_page_link()` binds a hotspot to an already-created
target page and finite displayed-page-space target point.

### Outlines

`quantapdf_composer_add_outline()` copies a non-empty UTF-8 title and returns
a nonzero 32-bit outline ID.

Parent ID zero means document-root. A nonzero parent must refer to an outline
created earlier in the same Composer. This parent-before-child rule makes the
tree acyclic by construction and gives deterministic sibling order without a
graph-normalization phase.

V1 outline destinations are internal page targets. URI outlines remain an
additive future extension.

## Coordinates

All public rectangles and target points preserve the normal Composer contract:

- PDF points;
- top-left origin;
- +x right;
- +y down.

The qpdf backend alone converts these coordinates into bottom-left PDF page
coordinates. Internal destinations are emitted as explicit `/XYZ` arrays.

## Backend representation

URI links become standard `/Annot /Subtype /Link` dictionaries with
`/A << /S /URI ... >>`. Internal links use `/Dest` arrays.

Outline nodes are indirect dictionaries linked through `/Parent`, `/Prev`,
`/Next`, `/First`, and `/Last`. `/Count` is positive for requested-open
nodes and negative for requested-closed nodes that have descendants.

All pages are created before navigation is emitted so every target page object
already exists.

## Ownership and limits

Caller strings are copied on success and charged to the existing Composer
resource-byte budget.

`quantapdf_composer_options` gains the append-only V2 field
`max_navigation_items`. Old V1 structure sizes retain their original exact
size and semantics. The default allows 1,000,000 combined link/outline items.

Failed mutations publish no link, outline, ID, or copied string.

## Verification

Focused tests cover:

- V1/V2 option-size compatibility and navigation capacity;
- invalid pages, rectangles, points, UTF-8, parent IDs, and open flags;
- URI and internal link round-trip through PDFium;
- Unicode outline titles and nested/sibling tree round-trip through qpdf;
- open/closed outline semantics;
- repeated-finish byte determinism and document reopenability.
