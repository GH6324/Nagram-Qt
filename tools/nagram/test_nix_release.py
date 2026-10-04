#!/usr/bin/env python3
"""Tests of nix_release.py: python3 tools/nagram/test_nix_release.py"""
import base64
import contextlib
import hashlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import nix_release as release


class NixReleaseTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.dir = Path(self._temp.name)
        self.out = self.dir / "nix" / "release.json"

    def tearDown(self):
        self._temp.cleanup()

    def run_main(self, name, content=b"archive"):
        archive = self.dir / name
        if content is not None:
            archive.write_bytes(content)
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = release.main(["--archive", str(archive), "--out", str(self.out)])
        return code, out.getvalue(), err.getvalue()

    def read(self):
        return json.loads(self.out.read_text())

    def test_names_the_version_and_the_hash_of_the_archive(self):
        code, _, err = self.run_main("Nagram-7.2.10.3-linux-x86_64.tar.xz")
        self.assertEqual((code, err), (0, ""))
        digest = base64.b64encode(hashlib.sha256(b"archive").digest()).decode()
        self.assertEqual(self.read(), {"version": "7.2.10.3", "hash": f"sha256-{digest}"})
        self.assertTrue(self.out.read_text().endswith("}\n"))

    def test_a_newer_release_replaces_the_entry_and_an_older_one_does_not(self):
        self.run_main("Nagram-7.2.10.9-linux-x86_64.tar.xz", b"nine")
        self.run_main("Nagram-7.2.10.10-linux-x86_64.tar.xz", b"ten")
        self.assertEqual(self.read()["version"], "7.2.10.10")
        code, out, _ = self.run_main("Nagram-7.2.10.9-linux-x86_64.tar.xz", b"nine")
        self.assertEqual(code, 0)
        self.assertIn("stays at 7.2.10.10", out)
        self.assertEqual(self.read()["version"], "7.2.10.10")
        self.run_main("Nagram-7.3.0.1-linux-x86_64.tar.xz", b"next")
        self.assertEqual(self.read()["version"], "7.3.0.1")

    def test_refuses_archives_that_are_not_a_stable_linux_release(self):
        for name in (
            "Nagram-7.2.10.3-beta-linux-x86_64.tar.xz",
            "Nagram-7.2.10-linux-x86_64.tar.xz",
            "Nagram-7.2.10.3-macos.zip",
            "td-update-linux-x64-7002010",
        ):
            code, _, err = self.run_main(name)
            self.assertEqual(code, 1, name)
            self.assertIn("not the Linux archive of a stable release", err)
        code, _, err = self.run_main("Nagram-7.2.10.3-linux-x86_64.tar.xz", content=None)
        self.assertEqual(code, 1)
        self.assertIn("cannot read", err)
        self.assertFalse(self.out.exists())

    def test_refuses_to_build_on_a_damaged_release_file(self):
        self.out.parent.mkdir()
        for content in ("{", "[]", '{"hash": "x"}', '{"version": "seven"}'):
            self.out.write_text(content)
            code, _, err = self.run_main("Nagram-7.2.10.3-linux-x86_64.tar.xz")
            self.assertEqual(code, 1, content)
            self.assertIn("is not a release entry", err)
            self.assertEqual(self.out.read_text(), content)


if __name__ == "__main__":
    unittest.main()
