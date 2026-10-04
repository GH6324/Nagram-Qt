#!/usr/bin/env python3
"""Tests of upstream_budget.py: python3 tools/nagram/test_upstream_budget.py"""
import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import upstream_budget

SUBMODULE = "Telegram/lib_x"
OLD = "1" * 40
NEW = "2" * 40


def git(root, *args):
    return subprocess.run(
        ["git", "-c", "user.name=t", "-c", "user.email=t@t", *args],
        cwd=root,
        capture_output=True,
        check=True,
        text=True,
    ).stdout.strip()


def setUpModule():
    # Commits start `git maintenance run --auto --detach`, which can still hold
    # files under .git when a test removes its temporary directory.
    count = int(os.environ.get("GIT_CONFIG_COUNT", "0"))
    patcher = mock.patch.dict(
        os.environ,
        {
            "GIT_CONFIG_COUNT": str(count + 1),
            f"GIT_CONFIG_KEY_{count}": "maintenance.auto",
            f"GIT_CONFIG_VALUE_{count}": "false",
        },
    )
    patcher.start()
    unittest.addModuleCleanup(patcher.stop)


class UpstreamBudgetTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.root = Path(self._temp.name)
        git(self.root, "init", "-q")
        self.write("Telegram/SourceFiles/core/a.h", "int a;\n")
        self.write("Telegram/SourceFiles/core/b.cpp", "int b;\n")
        self.write("lib/x.txt", "x\n")
        git(self.root, "add", ".")
        git(self.root, "update-index", "--add", "--cacheinfo", f"160000,{OLD},{SUBMODULE}")
        git(self.root, "commit", "-q", "-m", "base")
        self.base = git(self.root, "rev-parse", "HEAD")
        self._policies = tempfile.TemporaryDirectory()
        self.policy = Path(self._policies.name) / "upstream.json"

    def tearDown(self):
        self._policies.cleanup()
        self._temp.cleanup()

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def write_policy(self, overrides=(), base=None, **budget):
        values = dict.fromkeys(upstream_budget.BUDGET_KEYS, 100)
        values.update(budget)
        self.policy.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "upstream": "test",
                    "base": base or self.base,
                    "source_root": "Telegram/SourceFiles/",
                    "own_include_prefixes": ["nagram/"],
                    "submodule_overrides": list(overrides),
                    "budget": values,
                }
            ),
            encoding="utf-8",
        )

    def run_main(self, *args):
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            result = upstream_budget.main(
                ["--root", str(self.root), "--policy", str(self.policy), *args]
            )
        return result, output.getvalue()

    def metrics(self):
        policy = upstream_budget.load_policy(self.policy)
        tree = upstream_budget.tree(self.root, policy["base"])
        return upstream_budget.measure(self.root, policy, tree)[0]

    def test_an_untouched_tree_measures_zero(self):
        self.write_policy()
        self.assertEqual(self.metrics(), dict.fromkeys(upstream_budget.BUDGET_KEYS, 0))

    def test_changed_upstream_files_count_by_added_lines(self):
        self.write_policy()
        self.write("Telegram/SourceFiles/core/b.cpp", "int b;\nint c;\nint d;\n")
        self.write("lib/x.txt", "y\n")
        metrics = self.metrics()
        self.assertEqual(metrics["upstream_files"], 2)
        self.assertEqual(metrics["upstream_added_lines"], 3)
        self.assertEqual(metrics["source_files"], 1)
        self.assertEqual(metrics["source_added_lines"], 2)

    def test_deleted_upstream_files_count(self):
        self.write_policy()
        (self.root / "Telegram/SourceFiles/core/b.cpp").unlink()
        self.assertEqual(self.metrics()["source_files"], 1)

    def test_added_files_are_free(self):
        self.write_policy()
        self.write("Telegram/SourceFiles/nagram/core/x.cpp", "int x;\n")
        self.write("Telegram/SourceFiles/core/new.cpp", "int n;\n")
        git(self.root, "add", ".")
        metrics = self.metrics()
        self.assertEqual(metrics["upstream_files"], 0)
        self.assertEqual(metrics["upstream_added_lines"], 0)

    def test_upstream_headers_are_distinct_and_skip_tests(self):
        self.write_policy()
        include = '#include "core/a.h"\n#include "nagram/core/y.h"\n#include <vector>\n'
        self.write("Telegram/SourceFiles/nagram/core/x.cpp", include)
        self.write("Telegram/SourceFiles/nagram/core/y.h", '#include "core/a.h"\n')
        self.write("Telegram/SourceFiles/nagram/tests/t.cpp", '#include "core/b.cpp"\n')
        self.assertEqual(self.metrics()["upstream_headers"], 1)

    def test_within_budget_passes(self):
        self.write_policy(source_files=1, source_added_lines=1)
        self.write("Telegram/SourceFiles/core/b.cpp", "int b;\nint c;\n")
        self.assertEqual(self.run_main()[0], 0)

    def test_over_budget_fails_and_names_the_metric(self):
        self.write_policy(source_added_lines=1)
        self.write("Telegram/SourceFiles/core/b.cpp", "int b;\nint c;\nint d;\n")
        result, output = self.run_main()
        self.assertEqual(result, 1)
        self.assertIn("over budget: source_added_lines.", output)

    def test_list_shows_the_changed_files(self):
        self.write_policy()
        self.write("Telegram/SourceFiles/core/b.cpp", "int b;\nint c;\n")
        self.assertIn("     1 Telegram/SourceFiles/core/b.cpp", self.run_main("--list")[1])

    def move_submodule(self):
        git(self.root, "update-index", "--cacheinfo", f"160000,{NEW},{SUBMODULE}")
        git(self.root, "commit", "-q", "-m", "move")

    def test_moved_submodule_fails(self):
        self.write_policy()
        self.move_submodule()
        result, output = self.run_main()
        self.assertEqual(result, 1)
        self.assertIn(f"Submodule {SUBMODULE} is at {NEW[:10]}", output)

    def test_moved_submodule_passes_when_overridden(self):
        self.write_policy(overrides=[SUBMODULE])
        self.move_submodule()
        self.assertEqual(self.run_main()[0], 0)

    def test_missing_base_says_how_to_fetch_it(self):
        self.write_policy(base="3" * 40)
        result, output = self.run_main()
        self.assertEqual(result, 2)
        self.assertIn(f"git fetch origin {'3' * 40}", output)

    def test_unknown_policy_key_is_rejected(self):
        self.write_policy()
        policy = json.loads(self.policy.read_text(encoding="utf-8"))
        policy["extra"] = 1
        self.policy.write_text(json.dumps(policy), encoding="utf-8")
        self.assertEqual(self.run_main()[0], 2)


if __name__ == "__main__":
    unittest.main()
