# Original yWorks Source→single C11 72-DPI RGB acceptance (private)

Parent: [Draft PR #556](https://github.com/qigao/QuantaPDF/pull/556),
qualified exact HEAD `3c83bbd120e9b4c3fb1fd46093cf0640b3a6a3b0`.
Unmodified original corpus file `yworks.clippath-cliprule-v1.pdf`
is **3,747 bytes**, SHA-256
`9d30a67c8194b6822f28ad9ed4848a57c7745e95d11b79267ac5141001a91783`.

## Strict 72-DPI device admission

The existing private device `qp_native_device_ir_render` now accepts
one distinctly named **authentic 3GS/7Clip Source** opt-in. The same
fixed stack projects all four frozen source PATH Clip leaves, borrows
three independently owned Clip root identities, and projects the three
ordered Source PATH paints. The original V5 3GS branch/Stroke entry
revalidates every node and GS and partitions the unchanged C11 4×4
sample quota into three conservative shares, with no unused-work
transfer, no second renderer/fallback and atomic zero-output refusal.
For a physical 1px original Stroke, **only 72 DPI** is admitted;
96/144 DPI remain strict UNSUPPORTED.

## Independent geometry goldens in existing test

The in-tree `native_source_stage_oom` test loads the **actual
unchanged original** via the production QPDF parser, validates complete
3GS/7Clip ownership and every C allocation-fault rollback, then
renders the complete graph through the SAME production C11 V5→V1
raster at 72 DPI. Explicit analytically derived RGB controls cover:

- White outside red Stroke; red 8/16 AA coverage at (40,39),
  (40,40) and (180,40) → RGB `FF7F7F`; W* center hole
  (100,40) stays white.
- The first **nonzero W** star has central winding magnitude two,
  retaining blue at (80,125).
- The second **even-odd W*** star has central winding magnitude
  two, therefore (180,125) stays white, but blue survives at
  (160,125). Outside both stars remains white.
- Complete byte-identical second render and pixels that remain owned
  after Composer IR destruction.
- Original three-layer conservative budget max is 8,000,000 checks.
  Both blue layers have a 96×90 clipped draw envelope, 16 AA
  samples and 13 combined paint+Clip edges:
  `96*90*16*13=1,797,120` each. `3*1,797,120=5,391,360`
  aggregate succeeds while 5,391,359 returns UNSUPPORTED/zero
  pixels. A 38-edge quota and 96/144 DPI also reject atomically.

No new test target, CI, installer, consumer project or PDFium oracle.
The public Source bridge and external corpus acceptance remain
**unchanged** pending an independent public 72-DPI original RGB gate;
the original stays `UNSUPPORTED/null Bitmap` until that complete
qualification. Keep TeXample unsupported, #213/#214/#233 OPEN, PRs
Draft/unmerged.
