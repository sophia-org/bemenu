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

This fork pins Sophia's public C sources at `d6267e6c8690f90327280c3daf5da161743d8589` in `vendor/sophia-shell`,
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
