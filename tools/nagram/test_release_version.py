#!/usr/bin/env python3
"""Tests of release_version.py: python3 tools/nagram/test_release_version.py"""
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import release_version as release

UPSTREAM = """\
AppVersion         7002010
AppVersionStrMajor 7.2
AppVersionStrSmall 7.2.10
AppVersionStr      7.2.10
BetaChannel        1
AlphaVersion       0
AppVersionOriginal 7.2.10.beta
"""
MINOR = """\
AppVersion         7003000
AppVersionStrMajor 7.3
AppVersionStrSmall 7.3
AppVersionStr      7.3.0
BetaChannel        0
AlphaVersion       0
AppVersionOriginal 7.3
"""
BETA = "NagramRevision 3\nNagramChannel  beta\n"
STABLE = "NagramRevision 3\nNagramChannel  stable\n"


class ReleaseVersionTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.root = Path(self._temp.name)
        (self.root / release.UPSTREAM).parent.mkdir(parents=True)

    def tearDown(self):
        self._temp.cleanup()

    def run_main(self, upstream, nagram, *arguments):
        (self.root / release.UPSTREAM).write_text(upstream, encoding="utf-8")
        if nagram is not None:
            (self.root / release.NAGRAM).write_text(nagram, encoding="utf-8")
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = release.main(["--root", str(self.root), *arguments])
        return code, out.getvalue(), err.getvalue()

    def test_prints_the_tag_of_the_release(self):
        self.assertEqual(self.run_main(UPSTREAM, BETA), (0, "v7.2.10.3-beta\n", ""))
        self.assertEqual(self.run_main(UPSTREAM, STABLE), (0, "v7.2.10.3\n", ""))

    def test_an_upstream_version_without_a_patch_keeps_three_parts(self):
        nagram = "NagramRevision 1\nNagramChannel stable\n"
        self.assertEqual(self.run_main(MINOR, nagram), (0, "v7.3.0.1\n", ""))

    def test_reads_files_with_windows_line_endings(self):
        code, out, _ = self.run_main(UPSTREAM.replace("\n", "\r\n"), BETA.replace("\n", "\r\n"))
        self.assertEqual((code, out), (0, "v7.2.10.3-beta\n"))

    def test_accepts_only_the_tag_of_the_release(self):
        self.assertEqual(self.run_main(UPSTREAM, BETA, "v7.2.10.3-beta"), (0, "", ""))
        for tag in ("v7.2.10.3", "v7.2.10.4-beta", "v7.2.10-pre.3", "7.2.10.3-beta"):
            code, out, err = self.run_main(UPSTREAM, BETA, tag)
            self.assertEqual((code, out), (1, ""), tag)
            self.assertIn(f"Tag {tag} does not match", err)
            self.assertIn("v7.2.10.3-beta", err)

    def test_rejects_bad_revision_files(self):
        for nagram in (
            None,
            "",
            "NagramRevision 3\n",
            "NagramChannel beta\n",
            "NagramRevision 0\nNagramChannel beta\n",
            "NagramRevision 03\nNagramChannel beta\n",
            "NagramRevision three\nNagramChannel beta\n",
            "NagramRevision 3\nNagramChannel alpha\n",
            "NagramRevision 3\nNagramRevision 4\nNagramChannel beta\n",
            "NagramRevision 3\nNagramChannel beta\nNagramUnknown 1\n",
            "NagramRevision 3 \nNagramChannel beta\n",
        ):
            code, out, err = self.run_main(UPSTREAM, nagram)
            self.assertEqual((code, out), (2, ""), repr(nagram))
            self.assertIn(str(release.NAGRAM), err)

    def test_rejects_an_upstream_file_without_the_version(self):
        code, out, err = self.run_main("AppVersionStrSmall 7.3\nAppVersionStr 7.3\n", BETA)
        self.assertEqual((code, out), (2, ""))
        self.assertIn(str(release.UPSTREAM), err)

    def test_the_repository_files_describe_a_release(self):
        self.assertRegex(release.release_tag(release.ROOT), r"^v\d+\.\d+\.\d+\.\d+(-beta)?$")


if __name__ == "__main__":
    unittest.main()
