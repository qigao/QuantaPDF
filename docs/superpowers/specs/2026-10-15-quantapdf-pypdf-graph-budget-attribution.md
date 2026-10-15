# #269 — Original pypdf sealed Graph capture budget diagnosis

Stacked on [Draft #339](https://github.com/qigao/QuantaPDF/pull/339), exact passed HEAD `13fc0297e1391241751d2a171e345cf618db20f8`.

The independently published 4-page CC-BY-SA PDF (272039 bytes,
SHA-256 `cc195eac510b81123de4f55ac9f8185f2120975a7b75a106460b98615737aacd`,
original Git blob `5686652e59ece0109c624da74edc5502ad746fbc`)
was strictly `UNSUPPORTED` under the existing small/default Graph fixture
budget. The earlier source-preflight status was a **Graph-capture-stage
limit** and did *not* by itself prove feature-specific rejection of
Text/Image XObjects/SMask.

#339 then established four real selected-Graph CropBox/Rotate/default
UserUnit page maps and exact 72DPI device bounds with an explicitly
larger but still limited test-only Graph capture
(`40000 nodes / 120000 edges / 4 MiB values / 4 MiB streams / 32 MiB owner
storage / max depth 256`); full Linux Release+Sanitizers, Windows and macOS
succeeded. This does not alter product/API/default limits.

## Budget attribution without a production fallback

This child runs two **independent read-only test observations** over the
same unmodified original bytes *before Source close*:

1. The existing default source-fixture Graph limits are expected to
   return exact `QUANTAPDF_ERROR_UNSUPPORTED` and a NULL Graph Snapshot
   (no partially published graph, no acquired secondary owner).
2. The explicitly expanded test-only geometry budget must return
   `QUANTAPDF_OK`, seal the Graph and verify four effective boxes,
   intrinsic rotations, default UserUnit and source-CropBox-to-device
   corner mappings. Graph model node/edge/byte counts and bounded
   owner storage are reported.

New atomic JSON fields record `default_graph_capture_status`,
`default_graph_capture_error_code`, `expanded_test_only_capture_status`,
`graph_node_count`, `graph_edge_count`, `graph_model_bytes` and
`graph_owner_storage_bytes`. Existing Linux Release/ASan+UBSan, Windows
and macOS CMake test/report artifacts must assert both outcomes and
nonzero actual resource counts. The evidence can show exactly which
independent resource limits are exceeded; it is not a reason to globally
increase the production default or add a fallback.

The original PDF is still too complex for the supported C11 Path/Clip
native-positive content subset. Its Graph geometry being valid says
nothing about its unsupported text, image transparency or annotations.

Issue #269 remains OPEN; license-review #336 OPEN; all PRs Draft /
DO NOT MERGE, formal external pixel-positive count zero for this
document and public SDK stays PDFium. #208 backend switch blocked.
