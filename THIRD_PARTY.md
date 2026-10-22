# Third-party components

QuantaPDF's backend stack pins the following components. Release
packages install these notices alongside the library; floating "latest" assets
and unverified system PDFium installations are not accepted.

## PDFium

- Version: `154.0.8021.0`, release `chromium/8021`
- Binary distributor: <https://github.com/bblanchon/pdfium-binaries>
- Upstream: <https://pdfium.googlesource.com/pdfium/>
- License: BSD-style terms plus the notices shipped in the artifact's complete
  `licenses/` directory
- Build boundary: V8 disabled, XFA disabled, partition allocator disabled

| Platform | SHA-256 |
|---|---|
| Windows x64 | `adac8ce034015427b5daa81f8eeddfcc8e84bc2a9f036f007890ff18bd4388c4` |
| Linux x64 | `685f7930cd184ea22cd77afe707c1cf53b173d18118b6e16cb213c9277d7cdc3` |
| macOS x64 | `0e770fda56c6726a08fab84c6306ad91eceb10589020ce3a407fad3ebcbe7bb2` |
| macOS arm64 | `994600fa28974ce09a1c51c35039e808a6bc8ea3839050322c101ab229ad5c96` |

## qpdf

- Version: `12.4.0`
- Upstream: <https://github.com/qpdf/qpdf>
- Package source: vcpkg baseline
  `f74a2eade17a628413746557d04db25ccf6e76f9`
- License: Apache License 2.0 or Artistic License 2.0
- Initial feature boundary: no optional OpenSSL, GnuTLS, or Zopfli feature

## libjpeg-turbo

- Version: `3.2.0`
- Upstream: <https://github.com/libjpeg-turbo/libjpeg-turbo>
- Package source: transitive qpdf dependency from vcpkg baseline
  `f74a2eade17a628413746557d04db25ccf6e76f9`
- License: IJG License and Modified (3-clause) BSD License
- Direct behavior boundary: Composer validates baseline JPEG input through the
  libjpeg decompression API without retaining decoded samples; qpdf `Pl_DCT`
  provides the existing private JPEG encoding path. No second JPEG
  implementation is added.
- Redistribution: binary installs copy the vcpkg-provided
  `share/libjpeg-turbo/copyright` notice to
  `share/quantapdf/licenses/libjpeg-turbo/copyright`, including the required
  IJG acknowledgement

The qpdf package's installed copyright file and PDFium's root `LICENSE` plus
complete `licenses/` directory are the authoritative redistribution notices.
The installed libjpeg-turbo copyright file is the authoritative notice for the
JPEG codec boundary.

## Adobe Core 14 font metrics

- Source: Adobe Font Metrics files for the 14 PDF Core Fonts, distributed by
  Matplotlib in `mpl-data/fonts/pdfcorefonts`
- Use: numeric advance-width tables for Composer Base 14 text layout and
  ascender/descender metrics for native loose text-selection cells
- License: the AFM files and accompanying notice permit use, copying,
  modification, and distribution for any purpose without charge, provided the
  copyright notices are retained. QuantaPDF derives numeric metrics only and
  does not redistribute the AFM files.

Copyright (c) 1985, 1987, 1989, 1990, 1997 Adobe Systems Incorporated. All
Rights Reserved.

The generated tables in `src/backend/base14_metrics.h` are prominently marked
as derived from, and therefore modified from, the source AFM data.

> This file and the 14 PostScript(R) AFM files it accompanies may be used,
> copied, and distributed for any purpose and without charge, with or without
> modification, provided that all copyright notices are retained; that the AFM
> files are not distributed without this file; that all modifications to this
> file or any of the AFM files are prominently noted in the modified file(s);
> and that this paragraph is not modified. Adobe Systems has no responsibility
> or obligation to support the use of the AFM files.
