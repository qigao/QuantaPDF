# #269 — quarantined independent pypdf/PyMuPDF original page geometry

**QUARANTINE ONLY; not formal external native-pixel-positive admission.**

This directory contains an exact, **unaltered independently published PDF**,
used to inspect raw /MediaBox, /CropBox and /Rotate metadata and the fail-closed
Source→Graph→Detached IR admission boundary. It is not part of
`tests/external_source_corpus/manifest.json`. The existing external corpus
does not allowlist CC-BY-SA-4.0; no relaxation or automatic license approval
is implied.

## Original identity and rights

- **Original upstream file**: [py-pdf/sample-files/027-cropped-rotated-scaled/cropped-rotated-scaled.pdf](https://github.com/py-pdf/sample-files/blob/02b8203bf416db3dbdb03438bc8278dfcb0555f3/027-cropped-rotated-scaled/cropped-rotated-scaled.pdf)
- **Immutable upstream revision**: `02b8203bf416db3dbdb03438bc8278dfcb0555f3`
- **Unaltered original length**: **272039 bytes**
- **SHA-256**: `cc195eac510b81123de4f55ac9f8185f2120975a7b75a106460b98615737aacd`
- **Original and vendored Git blob SHA-1**: `5686652e59ece0109c624da74edc5502ad746fbc`
- **Creator**: Robert Wolff, 2026, credited in upstream [create.py](https://github.com/py-pdf/sample-files/blob/02b8203bf416db3dbdb03438bc8278dfcb0555f3/027-cropped-rotated-scaled/create.py), SPDX-FileCopyrightText / SPDX-License-Identifier `CC-BY-SA-4.0`
- **Publisher's license statement**: [README.md](https://github.com/py-pdf/sample-files/blob/02b8203bf416db3dbdb03438bc8278dfcb0555f3/README.md) states that each PDF in the source repository is CC BY-SA 4.0; [upstream LICENSE](https://github.com/py-pdf/sample-files/blob/02b8203bf416db3dbdb03438bc8278dfcb0555f3/LICENSE) is retained here unmodified as `CC-BY-SA-4.0-LICENSE.txt`.
- **Independent source-specific review**: [QuantaPDF #336](https://github.com/qigao/QuantaPDF/issues/336), **OPEN**; asset-level image-material rights and share-alike obligations still require review before any official corpus admission.

## Raw source page metadata (exact original bytes, NOT inferred from rendering)

| Selected page | MediaBox | CropBox | /Rotate | /UserUnit | Visible points after intrinsic rotation |
|---|---|---|---:|---:|---|
| 0 | [12 12 612 732] | [132 108 516 612] | 0 | absent → 1 | 384 × 504 |
| 1 | [9 9 459 549] | [99 81 387 459] | 90 | absent → 1 | 378 × 288 |
| 2 | [11 7 561 427] | [121 63 473 357] | 180 | absent → 1 | 352 × 294 |
| 3 | [11 13 561 793] | [121 117 473 663] | 270 | absent → 1 | 546 × 352 |

These source dictionaries are simple and uncompressed. This diagnostic
records their **raw immutable declarations**, not a general PDF-object parser.
The same file contains original Content Streams with text, image XObjects,
soft masks/transparency and extensive annotations; all are outside the
currently admitted bounded private C11 Fill+W Clip pixel positive.
**It has no /UserUnit entry**, so it cannot qualify nonunit UserUnit
behavior despite the project's separate generated-source positive tests.

## Qualification rules

1. Verify exact original bytes before and after all observations using
   SHA-256 and Git blob identity. Rejection must not overwrite source bytes,
   create symlinks or import hidden network resources.
2. Run independent selected-page typed `Source → Graph → Detached IR`
   observe preflight with explicit limits for pages 0–3. Record actual
   `native_lower_status` and keep `original_pdfium_pixel_oracle=not_run`,
   `native_raster=not_run`, external positive count 0.
3. A source-preflight exception or infrastructure parse error is **not**
   evidence of correct feature rejection; classify it separately and
   require correction before calling this a negative conformance.
4. Reported raw /CropBox and /Rotate do **not** imply an original-PDFium
   native raster parity pass. No official external manifest modification.
5. Keep #269 OPEN, #336 under review, intake PR **Draft / DO NOT MERGE**,
   installed/public SDK PDFium, #208 cutover blocked.

The raw original is published under CC-BY-SA-4.0 and must remain attributed.
No adapted/rewritten PDF is checked in. QuantaPDF's other source/code
licenses are not inferred to cover this original or vice versa.
