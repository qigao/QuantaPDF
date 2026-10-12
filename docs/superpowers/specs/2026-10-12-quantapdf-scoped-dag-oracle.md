# Scoped nested Clip DAG — independent PDFium pixel conformance

Tracks [#214](https://github.com/qigao/QuantaPDF/issues/214) after
the private scoped native Clip DAG V1 implementation (PR #227).

The test-only `quantapdf.native_scoped_dag_oracle` builds one canonical
immutable IR containing three ordered opaque Path operations:

- Red Path with a nested Clip DAG (three leaves, two intersection nodes).
- Blue Path with a direct Clip Path shared with the red operation's DAG.
- Green Path without a Clip resource, drawn outside both prior regions.

The **same IR** is consumed via the provider-free C11
`quantapdf_ir_raster_scoped_operations_v1` and, independently, through
`quantapdf_qpdf_compose` to a PDF followed by the public PDFium rasterizer.
Cases verify exact interior red/blue/green/white samples, ordinary paint
order, reversed red/blue order, fractional nested-leaf translation and
cycle failure with no native pixel or PDF bytes published.

The native renderer must produce byte-identical pixels across repeats.
Cross-renderer average per-channel RGB difference must be <=10; at most
1/12 of pixels may differ by more than 64 in any channel. Tolerances do
not replace investigating a large systematic mismatch. PDFium and QPDF
are linked in this **one oracle target only**, never in the separate
standalone C11 native scoped DAG test, native runtime or public ABI.

Still excluded: more than four opaque operations, arbitrary PDF q/Q stream
interpreter, stroke+Clip, transparency/soft masks/groups, text and image
rendering, annotation and extraction parity, and cutting PDFium out of the
product SDK. These remain under #214/#213/#208.
