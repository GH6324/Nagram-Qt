#!/usr/bin/env python3
"""Tests of gcc_check.sh: python3 tools/nagram/test_gcc_check.py"""
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent / "gcc_check.sh"
BASE = json.loads((SCRIPT.parent / "upstream.json").read_text())["base"]
# Stands in for a tool: records each call, git lists the changed files.
FAKE = """#!/bin/sh
here=$(dirname "$0")
tool=$(basename "$0")
number=$(ls "$here" | grep -c "^$tool\\.call\\.")
: > "$here/$tool.call.$number"
[ $# -eq 0 ] || printf '%s\\0' "$@" > "$here/$tool.call.$number"
if [ "$tool" = git ]; then cat "$here/changed.txt"; fi
if [ -f "$here/$tool.fails" ]; then exit 1; fi
"""
OBJECTS = "Telegram/CMakeFiles/{target}.dir/Debug/SourceFiles/{source}.o"
STATEMENT = "build {object}: CXX_COMPILER__{target}_unscanned_Debug /usr/src/{source} || order\n"
BUILT = (
    ("Telegram", "core/update_checker.cpp"),
    ("Telegram", "core/application.cpp"),
    ("Telegram", "nagram/core/regex.cpp"),
    ("test_nagram", "nagram/core/regex.cpp"),
)


def fake(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(FAKE)
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


def calls(path):
    found = sorted(path.parent.glob(f"{path.name}.call.*"), key=lambda call: int(call.suffix[1:]))
    return [call.read_text().split("\0")[:-1] for call in found]


@unittest.skipIf(sys.platform == "win32", "runs a bash script")
class GccCheckTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.dir = Path(self._temp.name)
        self.bin = self.dir / "bin"
        for tool in ("docker", "git", "cmake"):
            fake(self.bin / tool)
        (self.bin / "changed.txt").write_text(
            "Telegram/SourceFiles/core/update_checker.cpp\n"
            "Telegram/SourceFiles/nagram/core/regex.cpp\n"
        )
        self.out = self.dir / "out"
        self.tree = self.dir / "tree"
        self.configure = self.tree / "Telegram" / "configure.sh"
        self.tests = self.tree / "out" / "nagram-tests" / "Debug" / "test_nagram"
        fake(self.configure)
        fake(self.tests)

    def tearDown(self):
        self._temp.cleanup()

    def environment(self, **values):
        base = {k: v for k, v in os.environ.items() if not k.startswith("NAGRAM_")}
        base["PATH"] = f"{self.bin}{os.pathsep}{base['PATH']}"
        return {**base, **values}

    def check(self, *arguments, **values):
        command = ["bash", str(SCRIPT), *arguments]
        environment = self.environment(**values)
        return subprocess.run(command, env=environment, capture_output=True, text=True, check=False)

    def inner(self, *sources):
        """Run the script meant for the image against a stand-in build directory."""
        self.check(NAGRAM_LINUX_OUT=str(self.out))
        docker = calls(self.bin / "docker")[-1]
        script = docker[docker.index("check") - 1].replace("/usr/src/tdesktop", str(self.tree))
        statements = self.tree / "out" / "CMakeFiles" / "impl-Debug.ninja"
        statements.parent.mkdir(parents=True, exist_ok=True)
        statements.write_text(
            "".join(
                STATEMENT.format(
                    object=OBJECTS.format(target=target, source=source),
                    target=target,
                    source=source,
                )
                for target, source in BUILT
            )
            + "build Telegram/lib_base/CMakeFiles/lib_base.dir/Debug/base/assertion.cpp.o: CXX\n"
        )
        return subprocess.run(
            ["sh", "-c", script, "check", *sources],
            env=self.environment(NAGRAM_CHECK_API="-D TDESKTOP_API_TEST=ON"),
            capture_output=True,
            text=True,
            check=False,
        )

    def test_passes_what_differs_from_the_upstream_base_to_the_image(self):
        cache = self.dir / "ccache"
        result = self.check(NAGRAM_LINUX_OUT=str(self.out), NAGRAM_LINUX_CCACHE=str(cache))
        self.assertEqual(result.returncode, 0, result.stderr)
        (git,) = calls(self.bin / "git")
        self.assertEqual(git[2:6], ["diff", "--name-only", "--diff-filter=d", BASE])
        (docker,) = calls(self.bin / "docker")
        self.assertEqual(docker[:4], ["run", "--rm", "--platform", "linux/amd64"])
        self.assertIn(f"{self.out}:/usr/src/tdesktop/out", docker)
        self.assertIn(f"{cache}:/ccache", docker)
        self.assertIn("CCACHE_DIR=/ccache", docker)
        self.assertIn("tdesktop:centos_env", docker)
        self.assertEqual(
            docker[docker.index("check") + 1 :],
            ["SourceFiles/core/update_checker.cpp", "SourceFiles/nagram/core/regex.cpp"],
        )
        self.assertTrue(self.out.is_dir())

    def test_the_image_and_the_cache_are_optional(self):
        result = self.check(NAGRAM_LINUX_OUT=str(self.out), NAGRAM_LINUX_IMAGE="image")
        self.assertEqual(result.returncode, 0, result.stderr)
        (docker,) = calls(self.bin / "docker")
        self.assertIn("image", docker)
        self.assertNotIn("tdesktop:centos_env", docker)
        self.assertNotIn("CCACHE_DIR=/ccache", docker)

    def test_compiles_the_objects_of_every_target_and_runs_the_tests(self):
        sources = ("core/update_checker.cpp", "nagram/core/regex.cpp", "platform/win/dlls.cpp")
        result = self.inner(*(f"SourceFiles/{source}" for source in sources))
        self.assertEqual(result.returncode, 0, result.stderr)
        (configure,) = calls(self.configure)
        for option in (
            "CMAKE_CONFIGURATION_TYPES=Debug",
            "CMAKE_COMPILE_WARNING_AS_ERROR=ON",
            "DESKTOP_APP_DISABLE_AUTOUPDATE=OFF",
            "TDESKTOP_API_TEST=ON",
        ):
            self.assertIn(option, configure)
        (cmake,) = calls(self.bin / "cmake")
        self.assertEqual(
            cmake,
            [
                *"--build out --config Debug --target test_nagram".split(),
                OBJECTS.format(target="Telegram", source="core/update_checker.cpp"),
                OBJECTS.format(target="Telegram", source="nagram/core/regex.cpp"),
                OBJECTS.format(target="test_nagram", source="nagram/core/regex.cpp"),
                *"-- -k 0".split(),
            ],
        )
        self.assertEqual(
            result.stderr, "Not in the Linux build: SourceFiles/platform/win/dlls.cpp\n"
        )
        self.assertIn("Compiling 3 objects of 3 sources.", result.stdout)
        self.assertEqual(calls(self.tests), [[]])

    def test_fails_when_no_source_has_an_object(self):
        result = self.inner("SourceFiles/platform/win/dlls.cpp")
        self.assertEqual(result.returncode, 1)
        self.assertIn("No object of the 1 sources", result.stderr)
        self.assertEqual(calls(self.bin / "cmake"), [])

    def test_a_failed_build_fails_the_check_without_running_the_tests(self):
        (self.bin / "cmake.fails").touch()
        result = self.inner("SourceFiles/core/update_checker.cpp")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(calls(self.tests), [])

    def test_needs_the_build_directory_and_takes_no_arguments(self):
        for arguments, values in (([], {}), (["--changed"], {"NAGRAM_LINUX_OUT": str(self.out)})):
            result = self.check(*arguments, **values)
            self.assertEqual(result.returncode, 2, arguments)
            self.assertIn("Usage:", result.stderr)
        self.assertEqual(calls(self.bin / "docker"), [])

    def test_reports_a_base_that_git_cannot_compare_with(self):
        (self.bin / "git.fails").touch()
        result = self.check(NAGRAM_LINUX_OUT=str(self.out))
        self.assertEqual(result.returncode, 1)
        self.assertIn(f"git fetch origin {BASE}", result.stderr)
        self.assertEqual(calls(self.bin / "docker"), [])


if __name__ == "__main__":
    unittest.main()
