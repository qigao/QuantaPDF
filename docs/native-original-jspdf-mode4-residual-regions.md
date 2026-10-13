# #269 — Mode4 original jsPDF residual error localization

**Draft / DO NOT MERGE.** Stacked on exact #289 verified HEAD
`348f935fd147af3adf0e6409e077a9aa0dc9ed0f`
([full CI #38479392892](https://github.com/qigao/QuantaPDF/actions/runs/38479392892)
SUCCESS 3/3).

The unchanged 3,344-byte upstream jsPDF original, SHA256
`179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e`,
is compared using original PDFium for every 596×842 device pixel.
Prior private mode4 C11 source-ordered Stroke→W Clip Fill has total RGB
absolute error 10,979 / 791 changed pixels / 0 severe >64, FNV64
`226faee70471ea2c`. No native-original pixel-positive is authorized.

This PR only extends the already independent original mode4 A/B test:
one more **read-only whole-page pixel scan** of existing privately rendered
Stroke, W Clip Fill, composite and original PDFium RGBA/RGB observations;
no additional rendering, new ownership, PDFium fallback or runtime change.

Seven disjoint source-specific windows distinguish bottom single-pass
horizontal Stroke, right and left vertical Stroke, top repeated Stroke,
red clipped Fill right boundary, faint bottom Clip fringe, and everything
else. Region JSON reports original pixels, changed/gross, RGB absolute
error, directional RGB bias (C11 brighter/darker), worst-channel delta
and its device coordinate. Eight fixed original-source witness pixels
report independent RGB and native Stroke/Fill alpha.

The CMake checker confirms seven regions cover all 501,832 original
pixels, reconcile with the already computed mode4 full-page
error/changed/gross/max, validate witness geometry and RGB, and preserve
the previous output digest. These windows must NEVER become production
heuristics. No passing error tolerance is invented or PDFium mechanism
asserted. #269 remains Open, public SDK stays PDFium, #208 cutover blocked.
