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
bounded larger test-only Graph capture. Its full Linux
Release+Sanitizers, Windows and macOS CI succeeded.

The first #340 macOS source report exposed the actual graph footprint:
**13,471 nodes**, **13,863 edges**, **179,055 model bytes**, and
**1,486,860 owner-storage bytes**. Compared against the default test
budget, only the **10,000 node cap** is exceeded. The 30,000-edge,
1 MiB value, 1 MiB stream, 4 MiB owner storage, and 256-depth limits
need no expansion (the aggregate 179,055 model bytes bounds both the
stream and ordinary-value storage).

This child therefore NARROWS the isolated Graph capture to
**16,000 nodes** (instead of 10,000) while retaining every other existing
budget field byte-for-byte. It does not alter any product/API/default
limits or introduce any runtime fallback.

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

## Current exact-head status

After the initial resource report, the final node-only cap and its strict
CMake verification have been submitted. Full Linux Release+Sanitizers,
macOS, Windows must pass on this *latest* HEAD before this specific
minimal-cap qualification can be reported as successful. The earlier
macOS count is evidence for the narrowing, not final node-only CI success.
