# TeXample M4b: physical page mapping checkpoint

Parent: qualified Draft [#563](https://github.com/qigao/QuantaPDF/pull/563), HEAD `d33ff8b0ef8de62abda23722498c94cee175ea50`. This checkpoint addresses the physical page mapping prerequisite of M4b. Full M4b, miter-join qualification, three-layer composition and public original RGB remain incomplete.

## Physical coordinates and buffer allocation

The unchanged original TeXample PDF is 1,396 bytes, SHA256 `f691ff46e64353ad285a5ddf24c82b956de67b5a6b3ed7d244167a301f36a284`, with MediaBox `[0 0 237.133 180.44]`. At 72 DPI its buffer is exactly `ceil(237133/1000) x ceil(180440/1000) = 238 x 181` pixels. The source-to-device transform is `[1 0 0 -1 0 180.44]`, not `[238/237.133 0 0 -181/180.44 0 181]`.

The new private physical planner shares the original checked decimal subtraction and integer quotient/remainder allocation calculation. Geometry uses DPI/72 independently of the buffer size; physical bounds retain the original decimal extents. Crop origin, UserUnit and quarter rotations are included in the source transform. Composer's float page-height anchor is corrected in double at consumption, without rewriting any source control point. The original FIT planner and its callers retain their existing contract. A FIT plan cannot opt in merely by changing its mode tag.

The same C11 V1 Fill/Clip and original M4a Stroke sampling loop intersects coverage with the physical page at every one of its 4x4 sample points. The unused right/bottom portion of the integer buffer retains the initialized background. No whole-pixel clipping, endpoint rescaling, additional renderer, public ABI, public Source admission or new CMake/CI target is introduced.

## Independent fringe controls and authentic Source curves

The existing device-plan test adds twelve original-page physical plans (three DPIs, four rotations), signed Crop/UserUnit controls, exact pixel-quota rejection, malformed-input isolation and FIT/physical cross-rejection. At 72 DPI the physical mapping is exactly one device pixel per source point; no float-derived ceil is used.

The existing Source-stage/OOM test adds an independent rectangle crossing all four page edges. It compares every output RGBA pixel against integer millipoint sample predicates, on opaque white and transparent backgrounds, both with and without an explicit Clip. This is an analytical coverage control, not a replacement TeXample document. For the original 72-DPI page:

| Region | Covered samples | Transparent red RGBA | Red on white RGBA |
|---|---:|---|---|
| Interior | 16/16 | `ff0000ff` | `ff0000ff` |
| Last column, before last row | 4/16 | `ff000040` | `ffbfbfff` |
| Last row, before last column | 8/16 | `ff000080` | `ff7f7fff` |
| Bottom-right pixel | 2/16 | `ff000020` | `ffdfdfff` |

The same existing Source test reads the untouched original PDF through the private Source M3 producer, borrows each actual original black closed two-ring `S` operation, retains its real `237.133f x 180.44f` semantic page and renders with the explicit physical plan. It checks repeated complete byte equality, original edge/interior/outside controls, white final row/column, Source ownership after rendering and detached image lifetime after IR destruction. The original per-edge conservative candidate preflight is reused: exact `N` succeeds, `N-1` rejects atomically, 191 edges and altered widths/joins refuse. The global 8,000,000 sample-test cap is unchanged. Old M4a integer-grid checks remain in the same test.

## Remaining gates

The private physical Stroke entry still uses the M4a segment-distance approximation. This checkpoint does not establish miter coverage at all subdivision vertices or independent full-image cubic-Stroke parity. It also does not compose orange `f*` under `W*` and the two Q-restored black `S` operations. That requires the original Clip identities and source order, a single conservative aggregate 8M work budget with exact N/N-1, independent ordered source-over RGB goldens, ownership/OOM recovery and strict external-corpus reconciliation before public cutover.

TeXample remains public `UNSUPPORTED` with a null Bitmap. The dependency stack stays Draft/unmerged, and Issues #213/#214/#233/#530 remain open. Local strict C11 device-plan and prior V1 raster regressions, plus an extracted exact C++ physical-fringe control, pass. Full original Source/QPDF and cross-platform qualification are provided only by the existing Native CI for the resulting exact HEAD; they must not be inferred from those local checks.
