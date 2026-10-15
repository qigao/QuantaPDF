# #269 — Disambiguating external Graph-budget rejection from content admission

Test-only isolated child of Draft #339, which already passed full Linux
Release+ASan/UBSan, Windows and macOS CI at its exact head
`13fc0297e1391241751d2a171e345cf618db20f8`.

## Why this gate exists

The unchanged independently published py-pdf/PyMuPDF original has four
nonzero-origin CropBox pages with 0/90/180/270 rotation. A standard-budget
selected-page source-to-IR preflight returned `UNSUPPORTED` for all pages,
but a later test showed **standard Graph capture itself** was rejected. By
widening only the bounded private **test** Graph budget, all four actual
sealed Graph page geometry→device projections became qualified.

Conflating these two facts as "Text/Image/Transparency was rejected by
Content lowering" would be wrong. A real content-stage observation must
first obtain the Graph successfully and then call the selected-page
semantics/lowering functions.

## Independent staged instrument

One source-input test opens the **same unchanged pinned external bytes**
once and performs a fixed seven-case Graph capture matrix:

1. Production-like original test default budget (expected UNSUPPORTED).
2. Explicitly larger, still bounded geometry test budget (expected OK).
3. Wide budget but default `max_nodes`.
4. Wide budget but default `max_edges`.
5. Wide budget but default `max_value_bytes`.
6. Wide budget but default `max_stream_bytes`.
7. Wide budget but default `max_storage_bytes`.

All budgets retain bounded depth and return explicit `OK` or
`UNSUPPORTED`, never a hidden or out-of-band fallback. The independently
owned wide captured Graph is retained while other snapshots are dropped,
then the original source document is closed.

For each of four selected pages, the test independently records
`quantapdf_graph_admit_native_page_group`,
`quantapdf_graph_snapshot_page_semantics`, and
`quantapdf_graph_snapshot_page_composer_ir` outcomes. Any failed
projection must publish zero Page, Operation and Clip/GraphicsState
resources. Successful projections are disposed without running C11 raster
or claiming pixel support. The report also records true Graph node/edge/
byte/storage occupancy and all seven trial statuses.

The wrapper proves SHA-256/size identity both before and after the run;
refuses source/report aliases; requires all four observed pages and all
seven named budget scenarios; and archives the exact report per CI
configuration: Linux Release and ASan/UBSan, Windows and macOS.

**Not yet qualified** until exact-child-head CI completes. The staged
report does not change the already qualified parent Graph geometric result,
published original bytes, production Graph limits, installed SDK,
formal external corpus, or source licensure. The upstream CC-BY-SA PDF
remains quarantined pending source-specific review in #336.
No PDFium or native C11 pixel rendering, no positive increment, no
production release, no merge.

QuantaPDF #269 stays OPEN. All stacked PRs Draft / DO NOT MERGE and #208
native SDK backend cutover remains blocked.
