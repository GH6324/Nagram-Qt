#!/usr/bin/env python3
"""Tests of drop_superseded_caches.sh: python3 tools/nagram/test_drop_superseded_caches.py"""
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent / "drop_superseded_caches.sh"
HASH_A = "a" * 64
HASH_B = "b" * 64
# Stands in for gh: lists the caches of caches.txt, records the deleted ids.
FAKE_GH = """#!/bin/sh
here=$(dirname "$0")
if [ "$1 $2" = "cache list" ]; then
  cat "$here/caches.txt"
elif [ "$1 $2" = "cache delete" ]; then
  echo "$3" >> "$here/deleted.txt"
fi
"""


@unittest.skipIf(sys.platform == "win32", "uses shell scripts")
class DropSupersededCachesTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.dir = Path(self._temp.name)
        gh = self.dir / "gh"
        gh.write_text(FAKE_GH)
        gh.chmod(gh.stat().st_mode | stat.S_IXUSR)

    def tearDown(self):
        self._temp.cleanup()

    def deleted(self, caches, *keys):
        lines = [f"{index} {key}" for index, key in enumerate(caches, 1)]
        (self.dir / "caches.txt").write_text("".join(line + "\n" for line in lines))
        subprocess.run(
            ["bash", str(SCRIPT), *keys],
            env={
                **os.environ,
                "PATH": f"{self.dir}{os.pathsep}{os.environ['PATH']}",
                "GITHUB_REPOSITORY": "owner/repo",
                "GITHUB_REF": "refs/heads/main",
            },
            capture_output=True,
            check=True,
        )
        path = self.dir / "deleted.txt"
        return path.read_text().split() if path.exists() else []

    def test_older_run_ids_are_deleted(self):
        caches = ["Linux-tdesktop-null-3", "Linux-tdesktop-null-1", "Linux-tdesktop-null-2"]
        self.assertEqual(self.deleted(caches, "Linux-tdesktop-null-3"), ["2", "3"])

    def test_nothing_is_deleted_until_the_kept_cache_exists(self):
        caches = ["Linux-tdesktop-null-1", "Linux-tdesktop-null-30"]
        self.assertEqual(self.deleted(caches, "Linux-tdesktop-null-3"), [])

    def test_other_configurations_are_kept(self):
        caches = [
            f"Windows-x64-libs-v2-{HASH_A}",
            f"Windows-x64-libs-v2-{HASH_B}",
            f"Windows-x64-libs-v2-release-{HASH_B}",
            f"Windows-arm64-libs-v2-{HASH_B}",
            f"Windows-x64-qt6-{HASH_B}",
        ]
        self.assertEqual(self.deleted(caches, f"Windows-x64-libs-v2-{HASH_A}"), ["2"])

    def test_several_keys_are_handled_independently(self):
        caches = [
            f"macOS-libs-{HASH_A}",
            f"macOS-libs-{HASH_B}",
            f"macOS-libs-release-{HASH_B}",
        ]
        deleted = self.deleted(caches, f"macOS-libs-{HASH_A}", f"macOS-libs-release-{HASH_A}")
        self.assertEqual(deleted, ["2"])

    def test_no_caches_at_all(self):
        self.assertEqual(self.deleted([], "Linux-libs-" + HASH_A), [])


if __name__ == "__main__":
    unittest.main()
