# Painted catalog views retain identity without menu pointers

The native client needs the catalog slot of the row actually drawn, not an index
reconstructed from row height or the menu's current selection after another edit.
The shared Cairo painter now has an optional item observer. Its background
rectangle is computed once and used by both Cairo and the observer. The ordinary
renderer calls the same painter without an observer; no layout or theme policy
is replaced by a Sophia-specific drawing implementation.

The CPU raster collects up to 32 item background rectangles, rounding physical
endpoints independently and intersecting with the allocated image. It refuses
an overflowing capture rather than silently truncating the set. The temporary
item references are borrowed until menu mutation. `bm_sophia_view_capture`
resolves them immediately through the installed catalog model and copies only
connection/catalog/slot identities and geometry. The copied selected slot is
present only when its item was observed in this paint. Pixel bytes still belong
to the raster; the resource uploader must copy them before repainting.

These are observed background rectangles, not final authorized hit targets.
Later painter decorations (counter, arrows, borders or scrollbar) may overlay
backgrounds; the client candidate builder still needs to apply its exact target
policy and allocation geometry. This observation API neither authorizes input
nor makes a UI revision Presented. The generic C lifecycle retains the offered
scene and activates only after matching Presented and focus facts.

`bm_sophia_view_edit` adapts an already-authorized lifecycle callback to actual
Bemenu editing. It does not ACK, advance protocol state, accept a local selection,
or launch an application. Accept remains in the protocol owner, which reserves
both ACK and activation storage before committing either. The production
controller must supply this callback through the lifecycle, not dispatch raw
records straight into menu operations.

## Evidence and limits

The device-hidden project gate with warnings as errors passes on the joined
source in Sophia's `.artifacts/bemenu-native-view-joined`. Controls cover observed
item identity, background pixels, equal ordinary/observed painter output, 1/1.25/2
scale, pagination, upward and horizontal layouts, filtering/empty rows and capture
overflow. The catalog/view control uses the actual wire catalog assembler, menu,
Cairo raster and semantic edit adapter. Changing selection changes pixels; filter
keywords select the authorized catalog slot; replacement frees old menu items
without changing previously copied view identities.

Three compiled negatives change the observed item, replace the selected slot
with the first row, and ignore capture overflow. Each fails its intended runtime
assertion. Restored source hashes and a restored positive project gate are kept
with the evidence. Earlier attempts separately retain an optimized C fixture
stack failure and an incorrect test expectation for upstream's swapped internal
green/blue storage; neither is a native failure.

The dependency is 42 exact public Sophia files at `c2ff3fcd` (full hash in the
vendor manifest), including presented/focus/input lifecycle and response
reservation controls. This is not the persistent `bemenu-sophia` controller,
live Session dual-component admission, native target policy, GPU/KMS testing or
physical-run acceptance. Those remain required on the
[critical path](../plans/b6m2n8cp-native-launcher.md).

## Decoration-aware native target interiors

The observer now also receives later decoration rectangles and the painter's
border interior. The pixel owner subtracts later row backgrounds, spacing,
scrollbar background, the horizontal right arrow, upward footer/title/filter,
and counter from previously observed rows. It keeps the largest remaining
rectangle per row, matching the wire's one target per catalog row. Covered
fragments are not made clickable just to preserve a wider target.

Rows and the final border interior round inward to full physical pixels;
occluders round outward. The rounded-border interior is conservative: its central
vertical band excludes corner wings even where some painted pixels survive.
Fully hidden rows are omitted. This changes pointer target area, not the upstream
painted image. Catalog slots and presentation/focus binding still authorize input;
geometry alone does not make a row actionable.

Device-hidden evidence at Sophia `.artifacts/bemenu-target-final` exercises actual
Cairo pixels for horizontal arrow/counter overlap, upward rows/counter, title
spacing plus scrollbar, and a fractional-scale rounded border. With row text
colored like its own background, every pixel in every resulting target must
still have that row's exact color. Ordinary unobserved painting is byte-identical
in each case. The initial upward-row expectation used catalog-index parity;
upstream uses paint-order parity, so that fixture expectation was corrected and
the diagnostic failure retained. No native rendering or pointer routing was run.

Two compiled mutations independently omit decoration coverage and border-interior
clipping; both fail the actual pixel assertion. Source hashes are restored and
the restored full project gate passes with warnings as errors.
