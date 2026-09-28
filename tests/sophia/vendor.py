#!/usr/bin/env python3
"""Offline snapshot mutation controls; no source checkout or network needed."""
import importlib.util
import json
import shutil
import tempfile
import unittest
from pathlib import Path

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('sdk_vendor', root / 'scripts/check-sophia-vendor.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class Snapshot(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='bemenu-sdk-pin-')
        self.addCleanup(self.temp.cleanup)
        self.pin = Path(self.temp.name) / 'sdk'
        shutil.copytree(root / 'vendor/sophia-desktop-sdk', self.pin)

    def manifest(self, mutate):
        path = self.pin / 'manifest.json'
        value = json.loads(path.read_text())
        mutate(value)
        path.write_text(json.dumps(value))

    def refused(self):
        with self.assertRaises(ValueError):
            checker.verify(self.pin)

    def test_exact_snapshot(self):
        revision, count = checker.verify(self.pin)
        self.assertEqual(revision, '1526a30bec4dacb19e7e285f3c1ada0704472fdc')
        self.assertEqual(count, 168)

    def test_missing_changed_extra_and_symlink(self):
        path = self.pin / 'source/src/sophia_shell_session.h'
        body = path.read_bytes()
        path.unlink()
        self.refused()
        path.write_bytes(body + b'\n')
        self.refused()
        path.write_bytes(body)
        extra = self.pin / 'source/extra'
        extra.write_text('unmanifested')
        self.refused()
        extra.unlink()
        path.unlink()
        target = Path(self.temp.name) / 'target'
        target.write_bytes(body)
        path.symlink_to(target)
        self.refused()

    def test_mode_is_part_of_the_pin(self):
        path = self.pin / 'source/src/sophia_shell_session.h'
        path.chmod(path.stat().st_mode | 0o111)
        self.refused()

    def test_manifest_paths(self):
        original = (self.pin / 'manifest.json').read_bytes()
        for name in ('/absolute', '../escape', 'src/./file', 'src//file'):
            with self.subTest(name=name):
                (self.pin / 'manifest.json').write_bytes(original)
                self.manifest(lambda value: value['files'].update({name: '0' * 64}))
                self.refused()

    def test_commit_and_tree_binding(self):
        path = self.pin / 'source/src/sophia_shell_session.h'
        path.write_bytes(path.read_bytes() + b'\n')
        digest = checker.hashlib.sha256(path.read_bytes()).hexdigest()
        self.manifest(lambda value: value['files'].update({'src/sophia_shell_session.h': digest}))
        self.refused()  # Matching manifest still cannot change the pinned tree.
        self.manifest(lambda value: value.update(revision='0' * 40))
        self.refused()

    def test_source_root_symlink(self):
        source = self.pin / 'source'
        target = Path(self.temp.name) / 'source'
        source.rename(target)
        source.symlink_to(target, target_is_directory=True)
        self.refused()


if __name__ == '__main__':
    unittest.main()
