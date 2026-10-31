# Original TeXample private M4b reference

`texample72-m4b.rgba` is a 238 x 181, row-major, unassociated RGBA8 reference (172,312 bytes). Alpha is 255 throughout, on the original opaque white page. SHA256:

`51e36fcf3a39e93724e1a3176286944883378df9b39c942147d52827607f957e`

Its only source is the unchanged 1,396-byte `tests/external_source_corpus/documents/texample.intersecting-rings-v1.pdf`, SHA256 `f691ff46e64353ad285a5ddf24c82b956de67b5a6b3ed7d244167a301f36a284`, under the existing external-source provenance/license record. No PDF is regenerated or altered. This fixture is mathematical expected data for the existing native Source/OOM test, not an additional corpus document or public Source V10 positive.

The independent `tools/qualification/texample_original_reference.cpp` freezes all 32 authored cubic segments after the literal translation and exact 180.44-point Y flip, retaining decimal control points and decimal 0.3985 stroke width in long double. It imports no QuantaPDF header, parser, implementation or bitmap. The reference uses polynomial horizontal-ray root bisection for even-odd Fill/Clip and isolates all quintic stationary-distance roots via Bernstein sign variation for closed smooth Stroke. A second-derivative interpolation error bound supplies a conservative query index only; no chord determines a reference hit. Endpoint stationary cases are included. Numerical sample-boundary ambiguity below 1e-10 is a generator failure, not an accepted tolerance.

The measured minimum squared-distance margin is `1.36743984063e-06`; the minimum horizontal-root/sample margin is `1.48120977872e-06`. A separately generated binary32-Source-coordinate reference gives the **same entire RGBA bytes**, so storage conversion does not change any original 4x4 coverage decision here.

Physical sample positions are `(x + (sx + 0.5)/4, y + (sy + 0.5)/4)` inside the original 237.133 x 180.44 point page, at 72 DPI. Source scale is exactly one; 238 x 181 is only the enclosing allocation. The original orange even-odd Fill is intersected geometrically with the original W* Clip at shared samples, then followed by the two Q-restored black closed Strokes. Each layer uses the existing V1 integer opaque source-over/coverage resolution, in source order.

To reproduce, compile the reference source directly with a C++20 compiler and pass an output `.rgba` path; compare its bytes and SHA256 to this fixture. No CMake project, installed SDK consumer, CI workflow or second production renderer is involved.
