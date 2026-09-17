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
