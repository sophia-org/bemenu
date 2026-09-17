---
id: r4s8m2kn
kind: concept
tags: [native, raster, ownership]
---
# Preserve Bemenu through an owned Cairo raster

The first Sophia adapter preserves libbemenu filtering, navigation, Unicode and
Cairo/Pango appearance. A raster owns its Cairo surface/context and Pango context;
painting returns borrowed native-endian premultiplied ARGB32 bytes, including
stride. This is NOT the shell wire pixel format. A future upload must copy/convert
into a tracked immutable resource before repaint/free, retaining that resource
until the exact protocol release. It must not lend this mutable buffer to a
pending upload or treat Prepared as Presented.

The local allocation ceiling is 16 MiB, dimensions at most 8192, scale 1 through
4. These are local validation limits, not negotiated quotas or GPU bounds.
The current helper requires at least 64 logical pixels of allocation height and
rejects text that cannot fit vertically. Per-connection negotiated limits must
also be checked before raster allocation in the transport integration.

The implementation reuses the upstream painter serially (including its shared
formatting scratch buffer). It is not a concurrent rendering API. The pixel
view expires on repaint/free. Tests retain a byte copy when comparing renders.

`make sophia` builds the optional backend. `BEMENU_BACKEND=sophia` is recognized
but refuses construction until the public admission/lifecycle client exists.
There is no fake input loop, automatic X/Wayland connection or executable launch.
The headless fixture initializes upstream menu state directly; it is not proof
of successful native backend construction.

Validation: strict build, deterministic repeat paint, changed selection pixels,
Unicode filtering, changed color pixels, integer/fractional scale, invalid sizes,
nonfinite scale and oversized font refusal. Device-hidden execution additionally
proved these controls need no display or GPU. No protocol/native acceptance.

The Clang UBSan run exposed an upstream callback-type violation in item teardown.
The fork now uses a void-pointer adapter for the list destructor instead of casting
bm_item_free to an incompatible function type. Restoring the upstream menu.c in
a disposable source archive reproduces the sanitizer failure. An unused painter
counter was also removed for strict Clang compilation; rendering is unchanged.

Separate compiled mutants forcing unit scale or resetting selection during paint
fail the pixel/scale controls. These runs are CPU/menu evidence only. The retained
GCC sanitizer attempt was unavailable because libubsan is not installed; the
successful run used the installed Clang 21 undefined-behavior runtime instead.
No sanitizer category was disabled. This is not leak or native acceptance.
