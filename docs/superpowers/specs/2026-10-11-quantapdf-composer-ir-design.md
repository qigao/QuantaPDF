# Composer lowering to internal IR

Implementation decision for [#151](https://github.com/qigao/QuantaPDF/issues/151),
building on #150. This implements the composition/export producer-consumer
boundary of #145; importing arbitrary PDF content into IR, direct IR rendering,
analysis and transforms remain #145 work.

## Evidence and decision

MED, fact: `composer.c` accumulates owned page, operation, resource and navigation
records, but `qpdf_composer.h` takes the public opaque Composer handle. Its writer
includes `composer_internal.h` and reads the builder's arrays directly. The form
and tiling-pattern builders call the same writer on temporary child Composers.
SVG rollback and font/navigation lowering also mutate the same records.

Introduce a private `quantapdf_ir` value owning those records. Composer embeds
one IR and remains the sole mutable producer. Context retention, admission
limits, allocated capacities and resource-budget accounting stay in Composer;
the IR owns payload pointers and committed counts. The PDF writer takes a const
IR and includes no Composer owner declaration. All three export paths use the
same interface, including nested form and tiling-pattern construction.

The initial record vocabulary reuses the existing Composer text, image, path,
glyph, gradient, clip, form and navigation value types. These describe working
content, not the public builder object. Keeping their established options and
resource IDs preserves current semantics while leaving private descriptors and
resource-registry changes to #145/#147. No external schema dependency is added.

## Ownership and compatibility

IR payload pointers have unique ownership. Zero initialization creates an empty
model. Only committed counts describe initialized records; failed appends and
SVG rollback retain their existing publication rules. The internal dispose
function releases the same payloads in the same order as the former Composer
destructor and resets the value. Disposal does not release a context: the
enclosing Composer retains the domain until disposal and shell cleanup finish.

Export borrows the model synchronously. The producer must not mutate or dispose
it during export, and the writer must not retain its pointers. Const access is a
contract for the entire reachable payload, not just the top-level structure.
Moving the uniquely owned model requires clearing the source; ordinary copying
does not create shared ownership. There is no public freeze, detach or clone API.

Keep input validation, resource admission, error codes, ordering, deterministic
PDF settings and source-buffer copying unchanged. Finish remains repeatable and
does not consume the model. The writer trusts internally admitted records;
untrusted IR deserialization is not introduced. A separate validation runtime
belongs to #149. Public ABI, exported symbols and library version stay unchanged.

## Alternatives and rollback

A temporary projection of the Composer arrays would hide its type but leave
ownership in the builder. An independent owning IR makes the data usable after
the builder shell is gone and gives future producers the same cleanup contract.
A deep snapshot on every finish would add copying and fallible allocations;
the synchronous borrow preserves current cost and failure behavior. Moving the
resource registry and all validation at once would combine separate semantic
changes, so those remain explicit follow-ups.

The migration touches Composer lowering, IR storage and backend export together.
There are no persisted IR bytes or consumer migration requirements. Reverting
the change restores the former private owner layout and writer signature.

## Verification

Add a formal private boundary test that moves a populated IR out of a Composer,
drops its builder/context, exports twice, compares both outputs with the public
finish result, and opens/renders/extracts the PDF through the public SDK. Include
text and copied image resource storage and repeat-safe disposal. Existing suites
cover paths, glyphs, fonts, gradients, clips, forms, patterns, SVG rollback,
navigation, limits and failure paths. Run focused IR/context/Composer tests,
then full Release and sanitizer CTest suites and normal SDK installation.
