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

`roboto-hello-{72,144}-m0-rotate{90,180,270}.rgba.z` and
`roboto-hello-96-m0-rotate90.rgba.z` cover original page `/Rotate`.
Each file is lossless zlib-compressed RGBA, using the existing test zlib
dependency. References rotate original quadratic coordinates before solving
roots, preserving the physical viewport before device ceil. At 96 DPI,
rotating an old Bitmap changes the sample phase. At 144 DPI, exact boundary
samples can also change their half-open membership after original rotation,
even though the page extents are whole pixels. Display rotation retains its
separate lossless Bitmap semantics. All seven references are identical at
60 and 80 Decimal digits; the three old Hello goldens are unchanged.

| DPI / original Rotate | Decoded dimensions | Decoded bytes | Decoded SHA256 |
| --- | --- | --- | --- |
| 72 / 90 | 64 x 128 | 32768 | `5538f86b44d95b1cc1b57c94ff5cfbfd6bb38140a7ecf455f391067c3d613d04` |
| 72 / 180 | 128 x 64 | 32768 | `891c886bf5c1e77500b78cd7e073a00a1763a35cd5843bbea437339bf3c4cb5a` |
| 72 / 270 | 64 x 128 | 32768 | `7760eff600ead9ec31f69ee1cc27e07e5706f1f7ddb38678e70f4382f0ec2762` |
| 96 / 90 | 86 x 171 | 58824 | `7c2adf26f139fee010b81cd829f040016ec9ee852da0a9883487d2db0e785795` |
| 144 / 90 | 128 x 256 | 131072 | `e9b2d8eb37a5611b28b039d25c2c60d1bcea62aacb3e7700b180f88e49b565a9` |
| 144 / 180 | 256 x 128 | 131072 | `4a25a7c5f85991f58f699897d882f19d4449b4606a86bff0f65f6324ff4f4fda` |
| 144 / 270 | 128 x 256 | 131072 | `79a1ea135e6e084d4a2e052f60d2b42802457e0f2e3ea75e34826cb61a678dea` |

For example, reproduce the 96-DPI reference with:

```sh
python tools/qualification/native_ttf_reference.py --case hello --dpi 96 --rotation 90 --compress --output-dir /tmp/ttf-reference
```


## Rectangle and TrueType M1

`roboto-decimal-{72,96,144}-m0.rgba.z` and
`roboto-decimal-shapes-{72,96,144}-m0.rgba.z` are independent original
237.133 x 180.44-point pages. The first paints original quadratic Hello,
a solid RAW RGB image and green Fill; the second paints the same image,
green Fill and blue Fill with no text. Each has three logical paints.
All six compressed RGBA references agree at 60/80 Decimal digits.

| Case / DPI | Decoded bytes | Decoded SHA256 |
| --- | --- | --- |
| decimal / 72 | 172312 | `a0c31b8ab6176fcb3d6bf7966f231213ebf03b01cfcd7007f85268865aa29814` |
| decimal / 96 | 305588 | `7135b7fdfb49673b0391b05ba2fa2987f7ffd60d07bf91501d70d2ba33a98be4` |
| decimal / 144 | 685900 | `150cc9a80e681516a3004d08b67f6933165416409be75462f6a25892f4eeb83d` |
| decimal-shapes / 72 | 172312 | `83e773f72e81de25070b4bd406476ff2036bedcec5e284a4ebcb66d8dd319f6a` |
| decimal-shapes / 96 | 305588 | `d0e986bafc1571f8964302d123b405a652b196aa1d15f1580e5913b5f5a6d500` |
| decimal-shapes / 144 | 685900 | `3260eeebc76d72cb591fe6c844766b4249d9ab7d4c9eaa6b3889c5a2ae35b4ab` |

```sh
python tools/qualification/native_ttf_reference.py --case decimal --case decimal-shapes --compress --output-dir /tmp/ttf-reference
```

`roboto-{mixed,reverse,background,late}-{72,96,144}-m1.rgba` use the same
independent original-quadratic reference, with exact Decimal rectangle
edges and original paint order. The background golden also qualifies
actual public Composer → serialized PDF → Source → native RGB output.
Regenerate with the existing `native_ttf_reference.py` command above.
M0 files remain byte-identical. No reference tool runs in production/CI.

## Original fractional glyph/Clip samples (M2b.2i)

`roboto-hello-{72,96,144}-m2b2i.mask16` stores the five original Hello
glyphs separately, in paint order. Each glyph has a row-major physical
page of little-endian uint16 values. Bit `4*sy+sx` is membership at the
global sample `(x+(sx+0.5)/4, y+(sy+0.5)/4)`, with `sx,sy` in 0..3.
Dimensions use the exact enclosing ceil of 128x64 points at DPI/72.
Glyph masks are not unioned or converted to 8-bit alpha.

| DPI | Dimensions | Bytes | SHA256 |
| --- | --- | --- | --- |
| 72 | 128 x 64 | 81920 | `cc4fa9c8ae8be60cd129b0bb497ad5f6ad4decd84dad5da98aa827323ba9389a` |
| 96 | 171 x 86 | 147060 | `8cb02a9e28041f28251a04b78667e21f8ad5244ef27db7224174306793ff6f47` |
| 144 | 256 x 128 | 327680 | `25ae2582a2eae77aa09c2edd1b5fc5591b636fdb22df9232c2bd0c555a23c92c` |

The existing independent original-glyf Decimal reference produces these
masks from the SHA-pinned Roboto fixture, with no production Source/C11
imports, cubic conversion, flattening or captured Bitmap. Regenerate with:

```sh
python tools/qualification/native_ttf_reference.py --case hello --sample-masks --output-dir /tmp/ttf-reference
```

60- and 80-digit generation yields identical masks. Collapsing each glyph
to coverage and preserving glyph paint order reproduces the complete old
Hello M0 RGBA at all three DPIs. Regeneration also preserves all 24 old
M0/M1 RGBA files byte-for-byte.

Genuine original W/W* fixtures 99/100 use Source Clip x=16.25..43.25,
Y-down y=26.25..41.75, partial green Fill and the existing RGB Image, in
both paint orders. The H stem has nonempty marginal glyph/Clip masks but
empty intersections at every DPI; curved glyph edges also have nonempty
partial intersections. Native tests compare every mixed RGBA byte with
the independently generated glyph masks and analytic rectangle/Image
samples. They additionally prove rounded 8-bit alpha multiplication would
change visible pixels. Public fractional admission remains separately gated.
