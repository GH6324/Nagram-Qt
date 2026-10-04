#!/usr/bin/env python3
"""Tests of update_feed.py: python3 tools/nagram/test_update_feed.py"""
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import release_version
import update_feed as feed

UPSTREAM = "AppVersion         7002010\nAppVersionStr      7.2.10\n"
PACKAGES = (
    "td-update-mac-arm-7002010",
    "td-update-mac-x64-7002010",
    "td-update-win-x64-7002010",
    "td-update-win-arm-7002010",
    "td-update-linux-x64-7002010",
)


class UpdateFeedTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.root = Path(self._temp.name)
        (self.root / release_version.UPSTREAM).parent.mkdir(parents=True)
        (self.root / release_version.UPSTREAM).write_text(UPSTREAM, encoding="utf-8")
        self.path = self.root / "nagram-updates.json"

    def tearDown(self):
        self._temp.cleanup()

    def publish(self, revision, channel, packages, tag=None):
        (self.root / release_version.NAGRAM).write_text(
            f"NagramRevision {revision}\nNagramChannel {channel}\n", encoding="utf-8"
        )
        suffix = "-beta" if channel == "beta" else ""
        tag = tag or f"v7.2.10.{revision}{suffix}"
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = feed.main(
                ["--feed", str(self.path), "--tag", tag, "--root", str(self.root), *packages]
            )
        return code, out.getvalue(), err.getvalue()

    def read(self):
        return json.loads(self.path.read_text(encoding="utf-8"))

    def test_lists_every_platform_of_a_release(self):
        code, out, err = self.publish(3, "stable", PACKAGES)
        self.assertEqual((code, err), (0, ""))
        entry = {"released": 7002010003}
        self.assertEqual(
            self.read(),
            {
                "armac": {"stable": {**entry, "link": "/v7.2.10.3/td-update-mac-arm-7002010"}},
                "mac": {"stable": {**entry, "link": "/v7.2.10.3/td-update-mac-x64-7002010"}},
                "win64": {"stable": {**entry, "link": "/v7.2.10.3/td-update-win-x64-7002010"}},
                "winarm": {"stable": {**entry, "link": "/v7.2.10.3/td-update-win-arm-7002010"}},
                "linux": {"stable": {**entry, "link": "/v7.2.10.3/td-update-linux-x64-7002010"}},
            },
        )
        self.assertIn("linux stable: 7002010003", out)

    def test_keeps_the_other_channel_and_the_other_platforms(self):
        self.publish(3, "stable", PACKAGES)
        code, _, err = self.publish(4, "beta", ["some/dir/td-update-linux-x64-7002010-beta"])
        self.assertEqual((code, err), (0, ""))
        result = self.read()
        stable = "/v7.2.10.3/td-update-linux-x64-7002010"
        beta = "/v7.2.10.4-beta/td-update-linux-x64-7002010-beta"
        self.assertEqual(
            result["linux"],
            {
                "stable": {"released": 7002010003, "link": stable},
                "beta": {"released": 7002010004, "link": beta},
            },
        )
        self.assertEqual(list(result["armac"]), ["stable"])

    def test_an_older_release_does_not_replace_a_newer_entry(self):
        self.publish(5, "stable", PACKAGES)
        before = self.read()
        code, out, _ = self.publish(4, "stable", PACKAGES[:1])
        self.assertEqual(code, 0)
        self.assertIn("armac stable: kept 7002010005", out)
        self.assertEqual(self.read(), before)

    def test_rejects_packages_of_another_release(self):
        for channel, package in (
            ("stable", "td-update-linux-x64-7002010-beta"),
            ("beta", "td-update-linux-x64-7002010"),
            ("stable", "td-update-linux-x64-7002009"),
            ("stable", "td-update-linux-x64-7002010-canary-3"),
            ("stable", "td-update-linux-arm-7002010"),
            ("stable", "Nagram-7.2.10.3-linux-x86_64.tar.xz"),
        ):
            code, out, err = self.publish(3, channel, [package])
            self.assertEqual((code, out), (1, ""), package)
            self.assertIn(package, err)
        self.assertFalse(self.path.exists())

    def test_rejects_a_tag_the_version_files_do_not_describe(self):
        code, _, err = self.publish(3, "stable", PACKAGES, tag="v7.2.10.4")
        self.assertEqual(code, 1)
        self.assertIn("v7.2.10.3, not v7.2.10.4", err)

    def test_rejects_a_feed_that_is_not_a_platform_map(self):
        for content in ("[]", "{", '{"linux": 1}'):
            self.path.write_text(content, encoding="utf-8")
            code, _, err = self.publish(3, "stable", PACKAGES)
            self.assertEqual(code, 1, content)
            self.assertIn(str(self.path), err)
            self.assertEqual(self.path.read_text(encoding="utf-8"), content)


if __name__ == "__main__":
    unittest.main()
