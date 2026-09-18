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
