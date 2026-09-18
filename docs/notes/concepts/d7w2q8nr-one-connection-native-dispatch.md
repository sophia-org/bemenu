---
id: d7w2q8nr
kind: concept
tags: [native, ownership, ipc]
---
# One connection dispatches catalog, upload and presented input

The Bemenu connection owner composes the published C owners instead of adding a
second transport or reimplementing focus/input checks. It owns one receive buffer,
one outbound FIFO, a shared transaction source, catalog assembly plus installed
menu identities, immutable upload state and the native presentation/input owner.
The caller owns the authenticated connected descriptor, polling/deadlines and
termination. Neither this owner nor its semantic edit callback connects to a
display, executes an application or starts a helper.

The first outbound frame requests exactly revision 7 and native-launcher
capabilities `0x9a0`. A wrong welcome or a ContentLimits grant from another
connection is terminal. Catalog may arrive before or after limits; native state
is constructed only after both are committed. The existing FIFO is retained
while applying negotiated limits; a still-owned hello must drain before its ring
capacity changes. Resources and input use those same negotiated owners.

Catalog End has a retained installation phase. A returned initialization refusal
keeps that exact borrowed wire frame; retry does not assemble End again or replace
already-installed menu items. Later records cannot pass it. Input similarly goes
through the generic native lifecycle with `bm_sophia_view_edit` as its real menu
callback. If response commit refuses after editing, the original input remains
borrowed until its retained response can transfer. The completed edit is not
replayed. Terminal results latch until the caller closes and disposes the owner.

Each visit writes at most 64 KiB and reads at most 64 KiB, with at most 16 complete
records (or the smaller negotiated service limit). The underlying I/O operations
also have their existing syscall bound. These are service-work bounds, not latency
or peer-receipt promises. Application deadline and allocation/render scheduling
are still required; this component is not a complete executable launcher.

## Evidence and limits

The device-hidden full project gate, with warnings as errors, is retained at
Sophia's `.artifacts/bemenu-connection-retained`. Its connection fixture uses real
private sockets, framing, negotiation, catalog assembly/install, Cairo view
capture, native lifecycle, menu editing and the shared response FIFO. The test
supplies allocation/resource/permit and native Prepared/Presented/focus records:
it does not run Session's resource store or a native renderer. Candidate submission
is explicitly seeded through the lifecycle, not a purported automatic scheduler.

The actual text edit changes the real menu filter and selected catalog rows,
produces an exact Consumed ACK and rejects replay. A forced returned response
commit refusal retains the original input and cannot append the text twice. A
separate forced lifecycle initialization refusal retains catalog End and the
following Opening until retry, without replacing installed menu items. Both
catalog/limits arrival orders and a 20-entry catalog spanning bounded service
visits pass. Wrong revision/capabilities and wrong grant are refused.

Four compiled controls fail behaviorally: consume a refused frame, ignore grant
identity, exceed the complete-record service bound and bypass the menu callback.
Source restoration and the restored positive project gate are retained. An
initial bypass-callback control reached a null-string fixture dereference; that
is retained separately, and the final test checks the missing filter explicitly
before comparing it. No native run, hardware action or live Session acceptance
was performed.

Remaining: allocation/permit/deadline and frame scheduling, final target policy,
immutable-upload-to-candidate/resource-retirement join, persistent executable and
renderer startup, then Session dual-component integration and exact-source gates.
The renderer constructor remains disabled. See the
[critical path](../plans/b6m2n8cp-native-launcher.md).

## Automatic allocation and immutable upload

The connection scheduler now starts the actual native allocation request when a
current Opening has matching OutputFacts. It chooses bounded logical dimensions
within output extent, popout size, resource bytes and coverage limits. It retains
the exact request transaction, opening and output facts only after enqueue. A
refusal is not retried on every visit for unchanged opening/facts. Explicit retry
and deadline policy still belong to the forthcoming complete controller.

Allocation replies must match grant, transaction, request and output; granted
geometry must agree with the captured scale/scale generation, have no parent,
reservation, margins or anchor, and fit the output and negotiated pixel bounds.
Pixel endpoints are checked independently against scaled logical endpoints. A
fractional origin can add a pixel beyond ceil(extent), so the request calculation
reserves that slack before choosing its dimensions and byte budget. This uses
the existing raster profile (scale 1 through 4), not a claim of arbitrary scale
support. Contradictory replies terminate before raster construction or upload.

On a current grant the scheduler paints the actual catalog view and hands its
pixels to the immutable upload owner. It copies the view's opening, state/catalog
revision, output-facts generation and allocation identity, and removes the borrowed
pixel pointer from the retained metadata. A subsequent mutable Cairo repaint
cannot change that upload. One resource record is queued per visit, using the
shared transaction source/FIFO; unchanged state does not create another upload.
No rendering/upload starts while the opening is closed or closing. Existing
allocation/resources remain retained; close/reopen, replacement and retirement
scheduling still need the full controller join.

Device-hidden `check-sophia` evidence is at Sophia's
`.artifacts/bemenu-auto-upload-final`. A private peer observes the actual request,
supplies a matching allocation and resource statuses, then compares every uploaded
chunk against the real prior Cairo image after repainting that image differently.
It reaches Resident without a hand-created resource or candidate. Seven mismatched
allocation variants refuse without a raster/upload, unchanged refusal does not
produce a request storm, and fractional 7/4 geometry produces the expected
1023-by-561 raster. Four compiled negatives remove transaction matching, bypass
geometry validation, omit fractional slack and retry unchanged refused requests;
each fails behaviorally. Restored source and a restored positive gate are retained.
An earlier fixture incorrectly put a chunk ordinal in the final status; that
separate failed attempt was fixed to the protocol's zero field.

This slice does not acquire a frame permit, submit a native candidate, select
final hit targets or present the uploaded pixels. Those steps, resource cleanup
across replacement/close, deadlines, application startup and live Session wiring
remain required. The previous fixture's supplied Presented facts are not relabelled
as evidence for this automatic upload path.
