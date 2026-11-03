# M2 R1: common typed operation compilation (#570)

The existing M2a, M2b.1 and M2b.2c whole-page admission checks remain exact.
After full resource/profile/device validation, they now create a bounded stack
plan of typed Text, optional Fill and Image operations and invoke one private
per-operation compiler. Each operation carries its source-order slot, frozen
Clip reference, limits and physical projection inputs. Text keeps original
physical coordinates; Fill/Image use the same device projections as before.

This is the first implementation slice of the existing #570 shared compiler
design, not arbitrary M2 page admission. No new route identifier, parser, IR
owner, renderer, public ABI or resource quota is added. Historical wrappers
still call the same strict page validator. Other V5 profiles are unchanged.

The private M2b.2g four-paint compiler also uses this operation compiler for
the final Q-restored GS0 Fill. Its own complete admission runs first; the
first three clipped operations compile through M2b.2c, and the fourth PATH
keeps the existing physical DPI projection and unclipped FULL work mode.
All four geometry slots belong to one V5 owner and one ledger. The original
half-page suffix reservation, eight-edge share and failure cleanup remain.
This private increment leaves public four-paint Bitmap admission to #592.

Preparation still occurs in Text/Fill/Image order, independent of paint order,
to preserve the qualified allocation/error and reservation sequence. The
existing four-slot owner stores geometry in original paint order, and the
unchanged V5 executor produces pixels. Black Text coverage conversion, fixed
budget shares, resource/input lifetime, physical grid and output rounding are
unchanged. No retained-owner layout or heap allocation is added.

Acceptance reuses the existing complete public original RGB references,
Source/OOM allocation and N/N-1 tests, prior refusals and owner teardown tests
on Linux Release, Linux ASan/UBSan, macOS and Windows LLVM-MinGW. Source-level
extraction alone is not runtime acceptance. General fractional Text/Clip,
post-BT W, wider orders/fonts/colors and other profiles remain separate gates.
