# Working on the Sophia fork

Read docs/style-guide.md and docs/notes/index.md. Preserve upstream behavior and
keep native transport separate from menu/filtering and Cairo raster ownership.
Run `make check-sophia EXTRA_WARNINGS=-Werror` for the optional backend.
Do not describe headless fixtures as native acceptance. No display/device tests
or installation are part of this gate. Source-length checks apply to new code.
