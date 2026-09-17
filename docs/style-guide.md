# Sophia fork implementation discipline

Keep upstream C and existing public libbemenu behavior. Split new code by owner:
menu state, owned raster, wire parsing, connection lifecycle, and client policy.
Use passive records and explicit bounded ownership transfers. Never execute an
application, clipboard helper or ambient display fallback in the native backend.
Application activation belongs to Session's authorized catalog policy.

Review source cohesion at 800 lines; reject new C sources over 1,000 lines.
Tests live outside production sources. `make check-sophia` enforces these limits
on `lib/renderers/sophia` and `tests/sophia`; extend that inventory when adding
client/wire modules. Existing upstream files are not silently grandfathered into
this fork's new-code allowance or reorganized merely to satisfy line counts.

Compile with warnings treated as errors. Test ownership, refusal and actual pixel
changes, not only metadata. Keep upstream renderers optional and unchanged.
A private-socket test supplies presentation facts; it does not prove native
presentation. A device-hidden raster test is CPU evidence only. Retain that scope
in notes and commit messages.
