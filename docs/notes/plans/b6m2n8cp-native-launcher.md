---
id: b6m2n8cp
kind: plan
tags: [native, launcher, critical-path]
---
# Native launcher critical path

Base: upstream Cloudef/bemenu 44b82f9f3fb9f04acfaa89471dddc05a20e16883.
Fork: sophia-org/bemenu. Existing Wayland/X11/curses remain supported by upstream
code; this work adds an optional renderer, not a UI rewrite.

1. Owned CPU raster and behavioral gate (implemented). Cairo/Pango preserve the
   menu; GPU composition remains Sophia's responsibility. No render-node grant
   is required by this client checkpoint.
2. Sophia t104–t106: independent admission and output-scoped launcher allocation,
   presented keyboard/text authority, exact catalog actions, bounded shared
   service/resources. Existing revision 6 does not provide this complete contract.
3. Sophia t107: reusable public C framing/partial I/O and lifecycle client. Use
   independently encoded fixtures; no Rust ABI or private Bemenu protocol.
4. Connect renderer and add bemenu-sophia catalog client. Keep bemenu-run PATH/exec
   behavior outside the confined native path. Clipboard helpers, IME, touch and
   arbitrary script menus are out of the first native product.
5. Headless two-client orchestration, stale/replay/overflow/refusal mutations and
   visual behavior; exact-source contained gates and signed checkpoints.
6. Separately attended two-output acceptance alongside Lom: WM-owned shortcut,
   typing/navigation, exact presented activation, dismissal/focus restoration,
   replacement/failure isolation, logout and resource/latency evidence.

No native install or run follows automatically from the headless gate.

## Implementation progress

Sophia `c7dea19a` introduces the actual shared epoch-store implementation and
grant-scoped compositor node identity. The existing single-shell transport now
uses that implementation through its compatibility facade. Session still needs
multi-component construction and routing; this is not native admission.

This fork pins Sophia's public C sources at `c2ff3fcdaf8eb0b57872bc5b7a879d854a44cc36` in `vendor/sophia-shell`,
including their BSD license, exact upstream paths and SHA-256 manifest. The
optional renderer compiles these sources without Rust or a Sophia checkout.
`make check-sophia` verifies the manifest and runs the independent typed catalog
reader against the pinned Rust golden frames, alongside the Cairo raster tests.
The library preserves a committed catalog through an incomplete replacement and
rejects stale/malformed/replayed data. It supplies display references, never
permission to execute.

The native constructor deliberately still refuses: revision-7 focused input,
independent protected admission, content lifecycle and the catalog-backed
application client are not yet joined. The tests do not establish a live launcher.


The revision-7 vendor update includes the shared strict UTF-8 validator and the
native-launcher structural codec/corpus (11 record kinds). The native corpus
checks payload structure only, not lifecycle or authorization.

The menu semantic-input adapter applies the 17 revision-7 commands through the
existing default Bemenu editing/filtering/navigation implementation. UTF-8,
control/bidi rejection, 256-byte event and 256-byte filter limits are checked
before text is applied. Accept returns a request to the future protocol owner;
it does not select/execute locally. Vim raw-key mode is refused. The caller still
must establish current event authority and own ACK/revision/activation state.
Headless tests exercise Unicode deletion, rejected-event preservation, bounded
filtering, navigation and actual Cairo pixel changes. This is menu behavior,
not a connected native backend or native acceptance.


The catalog/menu bridge copies a committed authorized catalog into real Bemenu
items, with exact connection/generation/slot display references. Unavailable
entries are omitted. Labels remain unchanged; optional owned matching text adds
catalog keywords to the existing default/case-insensitive token matcher. Ordinary
items retain the upstream label-only path, and exact/prefix ranking still uses
the visible label. No command paths or execution are introduced.

The bridge stages all new items before replacing an owned menu. It refuses stale
or other-connection catalogs and external menu ownership, and clears the prior
filter cache on replacement. It accounts for upstream nonempty item replacement
freeing only the pointer list, while empty replacement frees the items too.
Controls cover real C catalog assembly, partial transfer, unchanged-query refresh,
keyword search, unavailable items, forged item metadata, four direct adapter
allocation failures, and 1000 populated/empty replacements. The focused menu
control passes device-hidden Clang address/undefined-behavior sanitizers; it does
not exercise Cairo, sockets or native presentation. Full `check-sophia` also
retains the existing Cairo gate. Compiled keyword and generation mutants fail.

The vendor pin includes the checked native candidate/chunk, allocation and
ACK/activation encoders and six inbound native record decoders. This bridge is
not the current presented model: later lifecycle code must copy exact row
identities into candidate/presented state and retain protocol obligations without
holding mutable menu items. Constructor enablement remains pending that join.


The pin also includes shared content-resource transfer and reply codecs, including
canonical whole-row Begin validation and bounded chunk copying. The resource
corpus runs in `check-sophia`. These functions neither retain pixels nor advance
resource generations; those duties remain with the client lifecycle owner, along
with negotiated limits and exact transaction matching. A failed Retire must not
be mistaken for ResourceReleased.

The published Sophia pin at `8148614707f6219c68f794583a5fd1c048ac626c` supplies
31 exact source/corpus/license files. ContentLimits/AdmissionRefused and the
common output/allocation/presentation/permit/action codecs are included, with
CandidateEnd, demand/cancel and ActionAck encoders. Both additional corpus tests
run in `check-sophia`; the complete device-hidden project gate passes with
warnings as errors. Evidence is retained in Sophia's
`.artifacts/bemenu-feedback-vendor`. The codecs validate wire shapes and coherent
bounds only. Presented/focus ownership, permit consumption, paired reply enqueue,
immutable raster retention and exact release/generation reuse still require the
client state machine. The renderer constructor remains disabled until that join;
this pin does not establish a usable native launcher or physical-run readiness.


The reusable client pin now includes the sole-writer owned FIFO and immutable
resource upload owner. ACK/activation pairs enter atomically under one byte and
record budget with reserved control capacity. Two copied-raster slots enforce
negotiated bounds; IDs/generations advance only when Begin is owned, and retiring
pixels remain owned until exact Released. Retire refusal preserves the resident
resource. The vendored controls run in `check-sophia`: actual private-socket partial
writes/backpressure and 1,000 pairs, plus 1,000 upload/release cycles while another
actual raster allocation remains held. The upload server responses are supplied
by the fixture, not the Session resource store. This does not establish Presented,
focus, menu input effects, candidate bindings or supervised native startup.


The published C pin at `c2ff3fcd` adds exact native candidate/Presented/focus/input
ownership and preallocated FIFO response reservations. The vendor gate executes
its private-socket controls. The actual Cairo painter now exposes bounded observed
row backgrounds, and the catalog/view adapter copies catalog slots and selection
without retaining menu pointers. See [painted catalog views](../concepts/g8p2m4vr-painted-catalog-views.md)
for controls and limits. The persistent controller must still join negotiation,
allocation/permits, upload/release, these views and native candidate/input service.
Final hit-target policy must handle painter decorations; the background observer
is not a substitute for that policy. Live Session admission and attended
acceptance remain open; `lom-test` is not ready from these component gates.


The [connection owner](../concepts/d7w2q8nr-one-connection-native-dispatch.md) now
joins actual negotiation, catalog/menu installation, resource reply dispatch and
presented input through one receive FIFO and one outbound owner. Real private
socket controls drive an actual Bemenu text edit and ACK, with supplied native
presentation and an explicitly seeded candidate. Refused catalog installation and
input-response transfer retain the borrowed frame without replaying completed
effects. Automatic allocation/render/upload/permit scheduling, final target policy,
application deadlines/executable startup and Session live integration remain open.
This is progress on t003, not physical-run readiness or completed t002/t003.


Automatic native allocation and real raster/resource upload now run through the
connection scheduler; [the connection record](../concepts/d7w2q8nr-one-connection-native-dispatch.md#automatic-allocation-and-immutable-upload)
retains bounds, origin identity, fractional rounding and private-socket evidence.
The next client join is frame permits and exact candidate submission, followed by
replacement/close resource scheduling and deadlines. Final hit-target policy and
persistent application startup remain incomplete. Resident bytes in a supplied
private peer do not establish native presentation or `lom-test` readiness.

The connection now joins resident views to demand/permit and native candidate
submission, promoting only matching Presented and retaining closed-view resources
until exact Released. See the [frame scheduling record](../concepts/d7w2q8nr-one-connection-native-dispatch.md#frame-permits-candidate-submission-and-closed-view-release).
The device-hidden peer supplies server outcomes; this is not live Session or
native acceptance. Final target geometry, permit cancellation/deadline races,
allocation reopen/replacement and executable/live supervision remain required.

Decoration-aware target interiors now exclude observed overlays and borders;
[the painted-view note](../concepts/g8p2m4vr-painted-catalog-views.md#decoration-aware-native-target-interiors)
records conservative rectangular semantics and actual pixel checks. This closes
the previously explicit raw-background target gap for the current painter.
Cancellation/expiry, reopen/topology and deadlines still require controller work,
followed by executable and live Session integration before `lom-test` readiness.

Reopen controls now retain old allocation/resource/candidate ownership across a
new Opening, including late old Prepared/Presented and a held Released response.
See [reopening ownership](../concepts/d7w2q8nr-one-connection-native-dispatch.md#reopening-preserves-the-old-owners).
Server close-to-revoke orchestration and idle cleanup servicing still need the
live Session join; these supplied-outcome tests do not complete that requirement.

Per-obligation client failure guards now refuse indefinite pending waits while
preserving owners for disconnect; healthy idle remains unbounded. See the
[deadline evidence and limits](../concepts/d7w2q8nr-one-connection-native-dispatch.md#pending-obligation-failure-guards).
These guards do not replace server cancellation/revocation or establish a native
latency target. The persistent event loop must drive them before physical readiness.

The persistent `bemenu-sophia --serve` executable now drives the connection owner;
[entry-point evidence](../concepts/d7w2q8nr-one-connection-native-dispatch.md#persistent-native-executable)
covers the real process against a private listener. It deliberately receives its
catalog only through Session, never stdin/application paths. No live profile or
installation was changed. Next critical join is protected Session supervision,
independent native opening/catalog/focus/content service alongside Lom, exact
cleanup/cancellation ordering, then the combined offline and physical-run harness.

Cancelled permit reception now preserves in-flight candidate ownership; see
[the exact terminal controls](../concepts/d7w2q8nr-one-connection-native-dispatch.md#cancelled-permits-preserve-candidate-ownership).
Standing cancellation and wrong/duplicate grant terminals are covered. Session's
late-record drain, actual removal/resource settlement and live close/reopen join
remain required; these supplied-response controls do not mark that path complete.

### Selected executable deployment boundary

The optional native executable now links the existing menu core directly. The
upstream libbemenu shared library and renderer-plugin targets still build the
same core. Cairo/Pango and other system libraries remain dynamic; this is not a
fully static binary. A selected shell executable must not need its whole build
directory exposed in the protected domain just to find a sibling private library.

A device-hidden Session test using the production ShellComponentLaunch and
supervisor reproduced the old binary failing to load libbemenu.so.0 before
negotiation. The rebuilt executable negotiates the native role and exact content
grant with only that binary exposed. The executable private-socket regression
also runs a relocated copy without sibling files. Full `make check-sophia
EXTRA_WARNINGS=-Werror` passes in the device-hidden fixture; logs and exact inputs
are in Sophia's `.artifacts/bemenu-standalone-link` and
`.artifacts/bemenu-protected-join`.

The protected test revokes/stops the connection immediately after negotiation;
Bemenu may report the resulting transport EOF as service code 4. It proves
protected admission and process reaping, not graceful UI close, actual catalog
rendering, native presentation, or physical readiness. Those remain subsequent
integration requirements.

### Native desktop SDK adoption, 2026-09-27

The Sophia t263 SDK extraction supplies a native file-session and launcher
lifecycle. The adoption branch starts at `7d2d239`, preserving the five local
commits ahead of origin. Its dependency moves to the complete immutable
`sophia-org/sophia-desktop-sdk-c` snapshot `6a59a13f026111a7a277943d71c1fdb090613a23`
under `vendor/sophia-desktop-sdk/source`. The offline checker binds every file,
executable mode and Git tree to the copied upstream commit. This is a signed
development pin; publication and a release identity remain pending.

The file path uses the SDK's native records and lifecycle with Bemenu's existing
menu, filtering and Cairo raster. Catalog rows enter the menu model directly;
there is no IPC frame translation. Session selects exactly one endpoint through
`SOPHIA_SHELL_9P_SOCKET` or `SOPHIA_SHELL_SOCKET`; IPC remains the rollback and
comparison path. No installation or configured default changes in this slice.

The snapshot/catalog baseline builds with warnings denied. Connection, catalog
(including file rows, allocation refusal and copied-string ownership), codec,
layout, pin, font-cache and CPU raster checks pass. The full `check-sophia` run
is still failed: its executable fixture exceeds the two-second hello watchdog.
The unchanged original binary reproduces the same failure; a syscall trace
shows the process scanning host NerdFonts files before hello. Retained local
logs are `.artifacts/sdk-baseline.log`, `sdk-executable-original.log` and
`sdk-executable.trace` in the adoption worktree. This does not classify later
file-adapter changes as passing; their tests and the production-export join
remain required.

The complete adapter now passes `make -j2 check-sophia EXTRA_WARNINGS=-Werror`
at nice 19 in a device-hidden, network-isolated bubblewrap domain. The first
strict adapter compile rejected the lifecycle test's 15,264-byte stack frame;
moving its whole-record fixture to static storage fixed it without relaxing the
12,500-byte warning. Both that failed log (`sdk-isolated.log`) and the passing
log (`sdk-isolated-after-stack.log`) remain under `.artifacts`.

The passing run mounts only DejaVu Sans and DejaVu Sans Mono at
`/usr/share/fonts`, with SHA-256 values
`7da195a74c55bef988d0d48f9508bd5d849425c1770dba5d7bfc6ce9ed848954` and
`b4a6c3e4faab8773f4ff761d56451646409f29abedd68f05d38c2df667d3c582`.
The host's `/usr/local/share/fonts` is absent. The existing executable watchdog
is unchanged. Fontconfig reports no writable cache directory in this read-only
domain; the tests still pass. The earlier ambient-font failure stays recorded.

The new adapter tests use the real native SDK lifecycle, file codec and Bemenu
menu/filter/Cairo, with supplied session outcomes. The linked test contains no
9P client symbols. They cover pixel content after editing, reserved ack room,
activation ordering, resource retirement, deferred catalog adoption, advisory
permit waits and terminal lost acknowledgements. Six snapshot mutation tests
and all existing IPC, executable, font and raster checks also pass. The actual
Bemenu executable against the production export is the next gate; no physical
presentation, launch-policy or daily-driver acceptance is claimed here.

The actual executable gate now passes against Sophia's production file export
and owners. Sophia integration `c23a38453` runs the artifact prepared by
`8d1680713` from this branch's signed `a354251a53368b1f99483b5015a20747afab9804`.
Binary SHA-256 is
`d64a527da40851404825f4bc307aad7286ecaf94517a6a8c1b3bfd12938cb044`.
The production protected supervisor hosts two openings, three candidates, one
text edit and one keyboard activation. Close retires resources, reopen resets
the query, and SIGTERM stops cleanly. The domain exposes one pinned JetBrains
Mono font and no devices; its executable, environment and mounts are checked.
Missing or mismatched artifact inputs fail before launch.

The gate scripts Session decisions and presentation observations, with frozen
content time; no physical rendering, launch policy, pointer, expiry or reconnect
coverage is claimed. Evidence is in
`~/.local/state/sophia/development-evidence/bemenu-files/live-a354251-first.log`.
The initial Sophia harness awaited review at that checkpoint. No application
install or default switch occurred.

The adoption is now merged locally as signed master `2e0fd78`, preserving the
five earlier unpushed commits. Sophia master `17b1709a4` contains the reviewed
helper and production harness. The final SDK pin is
`a0ab8c853fe56b68e01ae69b82d06c15fc177484`, including the custody-acknowledgement
ordering fix and its caller guidance. The adapter acknowledges each processing
pass and fetches announced objects promptly, including catalogs whose UI
adoption must wait until close.

The strict isolated gate passes for source
`fc79f64d722b09b6b4f08fd538d74ee2b1dbf35e`; the fresh signed-source artifact has
SHA-256 `81cf4008d43e59cc944de141737de9404384922415846ee9f628636a9b5cd678`.
The live production-export gate again passes two openings, three candidates,
one edit and one keyboard activation. Evidence: adoption worktree
`.artifacts/sdk-isolated-a0ab8c8.log` and Sophia development evidence
`bemenu-files/{prepare-fc79f64,live-fc79f64}.log`. The integration remains local;
publication, installation and attended acceptance are separate.

### 9P-only launcher, 2026-09-27

The operator retired Bemenu's IPC path. The native executable now requires a
nonempty `SOPHIA_SHELL_9P_SOCKET` and refuses any presence of
`SOPHIA_SHELL_SOCKET`, including an empty value or both variables together.
The r7 capability request remains `0x9a0`, and the file negotiation record is
unchanged: `bemenu_native status=negotiated revision=7 epoch=N wire=9p`.

The old connection owner, IPC catalog entry point and IPC-only test targets
are removed. The existing file adapter continues to use the standalone C SDK's
session and native lifecycle. The immutable SDK snapshot still contains its
optional compatibility sources for other consumers; this product builds with
`WITH_IPC=0` and links only the desktop and 9P archives. An executable symbol
check rejects linkage of legacy shell or WM entry points.

Catalog menu, copied-string ownership, allocation-refusal, filter and Cairo
assertions use file catalog objects. The file adapter tests retain input,
activation, retirement, close/invalidation and reopen coverage. SDK file tests
cover framing, records, custody and event acknowledgements. The independent
executable fixture is ported to 9P, preserving the wrong-revision and startup
refusal checks and two close/reopen allocation exchanges. These are supplied
Session outcomes; the external production-owner gate remains separate.

The strict gate passed on the first run in a private source copy with
`make -j2 check-sophia EXTRA_WARNINGS=-Werror`, nice 19, devices and network
hidden, no display environment and only two isolated DejaVu fonts. All seven
SDK file tests, 16 spec digests, the menu and adapter tests, six snapshot
mutations, layout, executable, font and CPU raster checks passed. The executable
fixture proved two openings, wrong-revision refusal, a bounded startup wait and
rejection of every retired-variable combination. Its symbol check found no
linked legacy shell or WM entry points. Evidence and the copied input inventory
are in `~/.local/state/sophia/development-evidence/bemenu-9p-only/`.

The C SDK pin remains `a0ab8c853fe56b68e01ae69b82d06c15fc177484`.

### Shared SDK alignment, 2026-09-27

The snapshot now pins `8decca1d73699d6750c9228ecbf6e27f589d965d` (167
files), matching the qualified WM SDK and Sophia's next candidate. This adds
the SDK's WM files/session layer without changing Bemenu's adapter or enabling
IPC. The exact-tree mutation test now expects that revision and inventory.
The private-copy `make -j2 check-sophia EXTRA_WARNINGS=-Werror` gate passes
with isolated DejaVu fonts, no network, display or devices, at nice 19.
It covers SDK file tests, menu/adapter tests, the two-opening executable
fixture and CPU raster. The earlier failure was the test's old expected pin;
the assertion was updated to the newly verified snapshot. Logs remain under
development-evidence/final-9p/bemenu-strict-run{2,3}.log. The new signed source
still needs its immutable artifact and external production-owner live gate.
The external artifact must be prepared from the new signed Bemenu commit and
its 9P production-owner test rerun; the IPC integration tests are retired. No
current session, installed release or upstream X11/Wayland/curses backend was
changed. The earlier IPC evidence above is historical and does not describe
the new executable.
