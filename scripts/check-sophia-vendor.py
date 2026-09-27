#!/usr/bin/env python3
"""Verify the exact SDK source tree offline; signer authorization is separate."""
import hashlib
import json
import re
import stat
from pathlib import Path

def require(condition, message):
    if not condition:
        raise ValueError(message)


def git_hash(kind, body):
    header = f"{kind} {len(body)}\0".encode()
    return hashlib.sha1(header + body).digest()


def tree(path, base, inventory):
    require(not path.is_symlink(), f"symlink: {path}")
    entries = []
    for child in path.iterdir():
        mode = child.lstat().st_mode
        require(not stat.S_ISLNK(mode), f"symlink: {child}")
        name = child.name.encode('utf-8')
        if stat.S_ISDIR(mode):
            entries.append((name + b'/', b'40000 ' + name + b'\0' +
                            tree(child, base, inventory)))
        else:
            require(stat.S_ISREG(mode), f"non-regular source: {child}")
            body = child.read_bytes()
            inventory[child.relative_to(base).as_posix()] = hashlib.sha256(body).hexdigest()
            permissions = b'100755' if mode & 0o111 else b'100644'
            entries.append((name, permissions + b' ' + name + b'\0' + git_hash('blob', body)))
    return git_hash('tree', b''.join(body for _, body in sorted(entries)))


def verify(root):
    require(not root.is_symlink(), 'SDK root is a symlink')
    for name in ('manifest.json', 'upstream.commit'):
        require(not (root / name).is_symlink(), f'symlink: {name}')
    manifest = json.loads((root / 'manifest.json').read_text())
    require(manifest['schema'] == 1, 'unknown manifest schema')
    require(manifest['repository'] == 'https://github.com/sophia-org/sophia-desktop-sdk-c',
            'wrong SDK repository')
    revision = manifest['revision']
    require(re.fullmatch(r'[0-9a-f]{40}', revision), 'invalid SDK revision')
    for name in manifest['files']:
        require(all(part not in ('', '.', '..') for part in name.split('/')),
                f'noncanonical source path: {name}')
    inventory = {}
    actual_tree = tree(root / 'source', root / 'source', inventory).hex()
    require(inventory == manifest['files'], 'SDK files differ from manifest')
    commit = (root / 'upstream.commit').read_bytes()
    require(git_hash('commit', commit).hex() == revision, 'commit revision mismatch')
    require(commit.split(b'\n', 1)[0] == b'tree ' + actual_tree.encode(),
            'source tree differs from pinned commit')
    return revision, len(inventory)


if __name__ == '__main__':
    root = Path(__file__).resolve().parent.parent / 'vendor/sophia-desktop-sdk'
    revision, count = verify(root)
    print(f'Sophia desktop SDK: {revision} ({count} exact files)')
