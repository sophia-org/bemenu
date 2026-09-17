#!/usr/bin/env python3
"""Verify the pinned public C dependency without a Sophia checkout or network."""
import hashlib
import json
import re
from pathlib import Path, PurePosixPath

root = Path(__file__).resolve().parent.parent / "vendor/sophia-shell"
manifest = json.loads((root / "manifest.json").read_text())
assert manifest["schema"] == 1
assert manifest["repository"] == "https://github.com/sophia-org/sophia-stack"
assert re.fullmatch(r"[0-9a-f]{40}", manifest["commit"])
assert manifest["license"] == "BSD-3-Clause"
assert {str(p.relative_to(root)) for p in root.rglob("*") if p.is_file()} == {
    "manifest.json", *manifest["files"]
}
for name, value in manifest["files"].items():
    path = PurePosixPath(name)
    assert not path.is_absolute() and ".." not in path.parts
    source = PurePosixPath(value["source"])
    assert not source.is_absolute() and ".." not in source.parts
    assert not (root / path).is_symlink()
    data = (root / path).read_bytes()
    assert hashlib.sha256(data).hexdigest() == value["sha256"], name
print(f"Sophia C dependency: {manifest['commit']} ({len(manifest['files'])} exact files)")
