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

## Frame permits, candidate submission and closed-view release

The next joined slice schedules a frame demand only for a current resident view.
The retained demand identifies its transaction, output and allocation. A permit
must match that demand and grant, respect negotiated limits and advance the permit
identity. The connection samples CLOCK_MONOTONIC, with an explicit clock entry for
fixtures; clock regression refuses. Its conservative local TTL starts at demand
enqueue, not reply receipt. Local expiry does not fabricate server cancellation:
the owner waits for the exact server expiry before issuing another demand.

The actual generic lifecycle queues Begin/Chunk then End with the copied view's
original opening, catalog/edit revision, allocation and observed rows. Candidate
feedback must name the original Begin transaction. Prepared leaves the candidate
pending; matching Presented promotes it. The previously shown resource remains
owned until replacement or close makes it eligible for retirement. Cleanup runs
while closed; Retire enqueue does not free the immutable bytes. Only exact Released
frees its slot and copied metadata. Stale unsubmitted views cancel/retire through
the existing upload owner, without another resource ledger.

Evidence at Sophia `.artifacts/bemenu-frame-final` uses real private sockets and
actual allocation request, Cairo raster, immutable upload, frame demand,
Begin/Chunk/End, generic presentation state and resource retirement. The peer
supplies allocation/status/permit/Prepared/Presented/Closed/Released; neither
Session resource accounting nor native presentation is exercised. Controls cover
wrong permit transaction, expired local TTL followed by exact server expiry and
renewed demand, Prepared remaining noninteractive, wrong candidate transaction,
closed-view byte retention and mismatched release refusal. The fixture's initial
Released record omitted its two-byte reason field; retained failed runs are not
positive evidence.

This is not the complete persistent controller. Remaining work includes final
decoration-aware target geometry (current submitted rectangles are observed row
backgrounds), cancellation/late-expiry races, deadlines, allocation release and
reopen/topology replacement, sustained edit/replacement controls, the executable,
and live Session integration. A permit queued before its local TTL is not proof
that the server received it before expiry. No hardware or live run was made.

Three compiled mutations fail their behavioral assertions: omit permit transaction
matching, allow a locally expired permit to offer, and treat Prepared as terminal.
Removing the connection's candidate-transaction guard alone does **not** defeat
the test: the generic lifecycle independently refuses the mismatch. That surviving
mutation is recorded as redundant-defense evidence, not a fourth mutation kill.
The disposable source was restored and its full project gate passed again.

## Reopening preserves the old owners

Closed only disarms the old view. The connection keeps the granted allocation
until its exact server revocation, and keeps retiring upload bytes until Released.
A later Opening does not reuse that allocation or reactivate its old presentation.
After revocation, the new request and raster carry the new opening and allocation;
the second upload slot can progress while the old resource still awaits release.
A retained permit for a different output/allocation waits for server expiry rather
than being offered against the replacement allocation. The offer boundary also
checks the same identities defensively.

The actual connection/private-socket fixture now opens twice while the first
resource is held in ReleasePending. It proves no early allocation request, exact
revocation enabling the new request, separate copied origins and resource IDs,
new upload progress, and old release settling only its own slot. A separate order
closes and reopens while a candidate is pending, then supplies that old candidate's
Prepared and Presented: both obligations drain, but the new opening remains
unpresented/unfocused and the old resource retires. Server outcomes are supplied;
this is not evidence of Session automatically revoking on close.

Evidence is Sophia `.artifacts/bemenu-reopen-final`. The live Session owner must
still orchestrate allocation revocation/candidate disposition and keep servicing
resources while closed. Explicit cancellation races and bounded failure deadlines
remain open. No socket on the desktop, display/device, or native worker was used.

## Pending-obligation failure guards

The connection now applies a five-second failure guard to startup, allocation,
permit, candidate, each upload/retirement slot, activation response, allocation
revocation, retained receive frame and stalled output. The guards observe actual
owner identities; unrelated incoming records or a different resource do not
renew an old obligation. Upload chunks share one transfer deadline. Retirement
is a separate phase. Only actual FIFO write progress renews the write watchdog.
Healthy negotiated idle time without obligations has no deadline.

A guard failure latches IO_ERROR with a typed timeout reason in the snapshot.
It leaves the request, response reservation, candidate and immutable pixel owners
intact. The caller must close the connection before disposal; Session must still
retire remote consumers independently. This is a bounded failure policy, not a
latency guarantee or successful cancellation protocol. The caller's event loop
must continue bounded visits; no background timer/thread was introduced.

Device-hidden evidence at Sophia `.artifacts/bemenu-deadlines-complete` covers
startup, exact allocation deadline boundary, terminal refusal of a later reply,
real upload waiting for Begin status, permit wait, Prepared waiting for Presented,
partial inbound header, and an owned uncommitted FIFO reservation. The latter is
an output-stall control, not kernel socket backpressure. Each tested timeout
retains its corresponding owner; repeated healthy idle visits remain valid.
Activation/revocation guard placement is source coverage here, not an exercised
native activation or real server revocation timeout. Explicit cancellation races,
persistent executable startup and live Session orchestration remain open.

## Persistent native executable

`make sophia` now builds `bemenu-sophia` as well as the optional renderer module.
The dedicated executable accepts `--serve` and uses only the explicit absolute
`SOPHIA_SHELL_SOCKET`. It checks a same-user filesystem socket and connected peer,
uses nonblocking/CLOEXEC transport, and bounds pending connect. These checks do
not attest confinement: Session remains responsible for protected launch and
admission. The renderer module constructor stays disabled; the native executable
uses a private empty libbemenu menu without discovering display plugins or reading
stdin. It keeps normal menu/filter/Cairo behavior, four rows and default styling.
Its displayed-count callback uses the captured view count for page navigation.

One event loop drives the existing connection owner, using readiness and bounded
20ms visits. Returned BUSY has a 10ms backoff even if more peer data is readable,
so an unconsumed admission-refused frame cannot create a readiness spin. SIGTERM
and SIGINT stop the loop; the descriptor closes before connection/menu disposal.
Unexpected protocol/I/O/timeout failure exits nonzero with a bounded stage/code
record. Negotiated is reported once and is not called protected or presented.
There is no application exec, clipboard helper, reconnection loop or ambient
X11/Wayland fallback. Session owns process restart and application activation.

The actual binary is exercised by `tests/sophia/executable.py` inside the
device-hidden project gate. A private Unix listener checks Hello, supplies
revision-7 welcome/limits/catalog/facts, observes two actual allocation requests
across openings with supplied refusals, then terminates the child. Missing endpoint
and unknown argument refuse; wrong revision and a silent-peer startup timeout
exit nonzero. This is process/socket/event-loop evidence, not protected admission,
resource presentation, child application launch or live Session integration.
Final evidence: Sophia `.artifacts/bemenu-executable-bounded`.
