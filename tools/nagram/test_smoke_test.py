#!/usr/bin/env python3
"""Tests of smoke_test.py: python3 tools/nagram/test_smoke_test.py"""
import contextlib
import io
import stat
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import smoke_test

LAUNCHING = """#!/bin/sh
echo "Launched version: 1.0" > "$2/log.txt"
exec sleep 30
"""
CRASHING = """#!/bin/sh
echo "Launched version: 1.0" > "$2/log.txt"
echo "GLib-GIO-ERROR: no schemas" >&2
exit 3
"""
SILENT = """#!/bin/sh
exec sleep 30
"""


@unittest.skipIf(sys.platform == "win32", "uses shell scripts")
class SmokeTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.dir = Path(self._temp.name)

    def tearDown(self):
        self._temp.cleanup()

    def script(self, text):
        path = self.dir / "app.sh"
        path.write_text(text)
        path.chmod(path.stat().st_mode | stat.S_IXUSR)
        return path

    def run_script(self, text, timeout, settle):
        output = io.StringIO()
        with contextlib.redirect_stderr(output), contextlib.redirect_stdout(output):
            result = smoke_test.main(
                [str(self.script(text)), "--timeout", timeout, "--settle", settle]
            )
        return result, output.getvalue()

    def test_passes_when_the_app_keeps_running(self):
        self.assertEqual(self.run_script(LAUNCHING, "10", "1")[0], 0)

    def test_fails_when_the_app_exits_after_launch(self):
        result, output = self.run_script(CRASHING, "10", "2")
        self.assertEqual(result, 1)
        self.assertIn("exited with code 3", output)

    def test_failure_shows_the_error_output_of_the_app(self):
        result, output = self.run_script(CRASHING, "10", "2")
        self.assertEqual(result, 1)
        self.assertIn(f"--- {smoke_test.STDERR}", output)
        self.assertIn("GLib-GIO-ERROR: no schemas", output)

    def test_fails_without_the_launch_line(self):
        result, output = self.run_script(SILENT, "2", "1")
        self.assertEqual(result, 1)
        self.assertIn("within 2 s", output)


if __name__ == "__main__":
    unittest.main()
