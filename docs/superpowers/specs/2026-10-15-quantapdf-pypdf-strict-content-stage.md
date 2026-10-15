# #269 — Strict nonpixel Content-stage rejection of a published original PDF

Stacked on Draft #341, whose [exact-head CI #38714521412](https://github.com/qigao/QuantaPDF/actions/runs/38714521412)
passed Linux Release+ASan/UBSan, Windows and macOS. Its four independent
reports proved that default Graph capture rejects a 13,471-node document
at the original 10,000-node budget, while wider and explicit test-only
Graph capture passes. In a successfully captured sealed Graph, four Page
Groups pass and four Content Semantics and complete Composer lowerings
return `UNSUPPORTED` transactionally, with zero partial outputs.

## What this child now requires

The diagnostic budget matrix gains an eighth explicit **minimal** working
test variant: only `max_nodes` grows from the default 10,000 to 16,000.
`max_edges=30,000`, `max_value_bytes=1MiB`,
`max_stream_bytes=1MiB`, `max_storage_bytes=4MiB` and
`max_depth=256` are left unchanged. The classifer uses **this
minimal captured Graph** rather than the previous wide Graph when
checking the Page Group and Content semantics of all four pages.

The C11-independent C++ observer and CMake report gate now fail unless:
- Default and node-limited-10k captures are `UNSUPPORTED`;
- Wide plus four other default single-limit trials are `OK`;
- The minimal node-only-16k capture is `OK`;
- All 4 selected Page Groups are `OK`;
- All 4 actual Content Semantics stages explicitly return
  `QUANTAPDF_ERROR_UNSUPPORTED`;
- All 4 full Composer lowerings return `UNSUPPORTED`, without publishing
  even one Page, Operation, Clip or GraphicsState;
- The original independently published CC-BY-SA PDF is byte-for-byte
  identical before and after; no PDFium original pixel oracle or native
  C11 raster runs, and native positive count remains **0**.

This is **not** a new parser feature, C11 Raster integration, SDK switch,
production Graph budget expansion or licensed-original positive. It is a
narrow strict source-backed negative conformance test with explicit
resource and semantic boundaries, under pending document-rights review
[#336](https://github.com/qigao/QuantaPDF/issues/336).

Keep [#269](https://github.com/qigao/QuantaPDF/issues/269) OPEN,
all PRs Draft / DO NOT MERGE, public SDK PDFium and #208 blocked.
