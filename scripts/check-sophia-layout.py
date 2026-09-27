#!/usr/bin/env python3
"""Apply the fork's source ceiling without rewriting upstream layout."""
from pathlib import Path

root = Path(__file__).resolve().parent.parent
paths = [*root.glob("lib/renderers/sophia/*.[ch]"), *root.glob("tests/sophia/*.[ch]")]
paths.extend(root.glob("tests/sophia/*.py"))
paths.extend(root.glob("vendor/sophia-desktop-sdk/source/src/**/*.c"))
paths.extend(root.glob("vendor/sophia-desktop-sdk/source/src/**/*.h"))
for path in paths:
    # The full immutable SDK also contains its schema-generated WM codec.
    # It is not Bemenu source; its exact bytes are pinned by the vendor check.
    if path == root / "vendor/sophia-desktop-sdk/source/src/sophia_wm_v1.c":
        continue
    count = len(path.read_text().splitlines())
    if count > 1000:
        raise SystemExit(f"{path.relative_to(root)}: {count} lines exceeds 1000")
    if count >= 800:
        print(f"review cohesion: {path.relative_to(root)} ({count} lines)")
print(f"Sophia layout: {len(paths)} files checked")
