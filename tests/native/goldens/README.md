# Original TeXample private M4b reference

`texample72-m4b.rgba` is a 238 x 181, row-major, unassociated RGBA8 reference (172,312 bytes). Alpha is 255 throughout, on the original opaque white page. SHA256:

`51e36fcf3a39e93724e1a3176286944883378df9b39c942147d52827607f957e`

Its only source is the unchanged 1,396-byte `tests/external_source_corpus/documents/texample.intersecting-rings-v1.pdf`, SHA256 `f691ff46e64353ad285a5ddf24c82b956de67b5a6b3ed7d244167a301f36a284`, under the existing external-source provenance/license record. No PDF is regenerated or altered. This fixture is mathematical expected data for the existing native Source/OOM test, not an additional corpus document or public Source V10 positive.

The independent `tools/qualification/texample_original_reference.cpp` freezes all 32 authored cubic segments after the literal translation and exact 180.44-point Y flip, retaining decimal control points and decimal 0.3985 stroke width in long double. It imports no QuantaPDF header, parser, implementation or bitmap. The reference uses polynomial horizontal-ray root bisection for even-odd Fill/Clip and isolates all quintic stationary-distance roots via Bernstein sign variation for closed smooth Stroke. A second-derivative interpolation error bound supplies a conservative query index only; no chord determines a reference hit. Endpoint stationary cases are included. Numerical sample-boundary ambiguity below 1e-10 is a generator failure, not an accepted tolerance.

The measured minimum squared-distance margin is `1.36743984063e-06`; the minimum horizontal-root/sample margin is `1.48120977872e-06`. A separately generated binary32-Source-coordinate reference gives the **same entire RGBA bytes**, so storage conversion does not change any original 4x4 coverage decision here.

Physical sample positions are `(x + (sx + 0.5)/4, y + (sy + 0.5)/4)` inside the original 237.133 x 180.44 point page, at 72 DPI. Source scale is exactly one; 238 x 181 is only the enclosing allocation. The original orange even-odd Fill is intersected geometrically with the original W* Clip at shared samples, then followed by the two Q-restored black closed Strokes. Each layer uses the existing V1 integer opaque source-over/coverage resolution, in source order.

To reproduce, compile the reference source directly with a C++20 compiler and pass an output `.rgba` path; compare its bytes and SHA256 to this fixture. No CMake project, installed SDK consumer, CI workflow or second production renderer is involved.

## Original physical 96/144-DPI TeXample (M4c)

The SAME unchanged 1396-byte source and frozen authored-decimal cubic table
produce the higher-DPI references below. Scale coordinates and original 0.3985
Stroke width by **DPI/72**; exact rational ceil determines only storage. These
files are mathematical outputs, never captured QuantaPDF or renderer images.
The existing 72-DPI reference and its SHA256 remain unchanged.

| DPI | Size | RGBA bytes | SHA256 |
| --- | --- | --- | --- |
| 96 | 317 x 241 | 305588 | `b5ed960392f23222c4f90f6e40a1aecd370cb71b99e50a8870b45fa2a1089d4a` |
| 144 | 475 x 361 | 685900 | `19c7350ffc8e41d073c6172e421a535826735c171389c33cf84034f0b82c1edc` |

Regenerate with the existing standalone C++20 reference tool (no new target):

```sh
c++ -std=c++20 -O2 tools/qualification/texample_original_reference.cpp -o /tmp/texample-reference
/tmp/texample-reference /tmp/texample96.rgba 96 authored /tmp/texample96.masks
/tmp/texample-reference /tmp/texample96-source.rgba 96 source-binary32 /tmp/texample96-source.masks
/tmp/texample-reference /tmp/texample144.rgba 144 authored /tmp/texample144.masks
/tmp/texample-reference /tmp/texample144-source.rgba 144 source-binary32 /tmp/texample144-source.masks
```

Both storage modes have identical **every-layer, every-subsample** masks and
complete RGBA output at each DPI. Optional mask serialization is explicitly
little endian, with original Fill/first-S/second-S layers in that order.
96-DPI masks: 458382 bytes, SHA256
`a7c1de9cd3513f9c4997d348dc4c73c0f2ab988a03dc13cbaf35891de9b6191f`.
144-DPI masks: 1028850 bytes, SHA256
`7ab38566303d32f44ca5180cc20ecbbdd85e01ddf2a97fa6c7044b59baf4f057`.
Authored minimum squared-distance/ray margins are respectively
`3.46860111069e-06 / 2.49920883902e-05` (96 DPI) and
`1.08539427591e-06 / 1.36524327952e-05` (144 DPI), above the tool's unchanged
1e-10 ambiguity refusal. Native tests compare full RGBA; the real public API
compares every RGB byte after independently checking reference opacity.


## Embedded TrueType text M0

`roboto-{hello,layers,tj,fringe}-{72,96,144}-m0.rgba` are independent
unhinted original-quadratic 4×4 references, including ordered glyph overlap,
composite é and physical page fringe. Reproduce offline with fontTools:

```sh
python tools/qualification/native_ttf_reference.py --output-dir /tmp/ttf-reference
```

The tool pins the existing Roboto fixture SHA-256 and uses 60-digit Decimal
quadratic roots. CI compares complete public RGB and native RGBA images;
fontTools is not a production or CI runtime dependency. See
`docs/conformance/native-truetype-text-m0.md` for scope and failure gates.
