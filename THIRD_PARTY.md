# Third-party components

The installed native runtime depends on pinned qpdf, ZLIB and JPEG codec
packages. Release SDKs redistribute their applicable notices. There is no
PDFium library, import target, renderer download or runtime.

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

The qpdf and installed libjpeg-turbo copyright files are the authoritative
redistribution notices for these private dependencies. No PDFium license
or binary is installed.

## Historical non-production source fixtures

Legacy third-party original PDFs in the repository are archival test
assets only, not part of the newly registered native CI or distributed
SDK. Their original publishers' licenses and attribution notices still
apply to those source files. They are not PDFium runtime dependencies.
Feature acceptance uses new deterministic native-only fixtures and
negative tests; no external/reference renderer comparison is performed.

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
