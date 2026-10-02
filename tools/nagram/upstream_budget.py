#!/usr/bin/env python3
"""Report and cap how much Nagram changes upstream Telegram Desktop files.

Every metric is compared with the upstream base commit recorded in
upstream.json. Only files that exist in the base count, so files that Nagram
adds are free. Budgets only ratchet down: lower them as hooks are
consolidated, and record the new base after every upstream sync.
The upstream_headers metric counts the distinct upstream headers that Nagram
sources include, which bounds the code an upstream API change can break.
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_POLICY = HERE / "upstream.json"
POLICY_KEYS = {
    "schema_version",
    "upstream",
    "base",
    "source_root",
    "own_include_prefixes",
    "submodule_overrides",
    "budget",
}
BUDGET_KEYS = (
    "upstream_files",
    "upstream_added_lines",
    "source_files",
    "source_added_lines",
    "upstream_headers",
)
SOURCES = (".h", ".hpp", ".cpp", ".mm", ".m")
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)
GITLINK = "160000"


class PolicyError(Exception):
    pass


def load_policy(path):
    try:
        policy = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise PolicyError(f"cannot read policy {path}: {error}") from error
    if not isinstance(policy, dict) or set(policy) != POLICY_KEYS:
        raise PolicyError(f"policy {path} must have exactly {sorted(POLICY_KEYS)}")
    if policy["schema_version"] != 1:
        raise PolicyError(f"unsupported policy schema {policy['schema_version']}")
    if not re.fullmatch(r"[0-9a-f]{40}", str(policy["base"])):
        raise PolicyError("base must be a full 40-character commit hash")
    budget = policy["budget"]
    if not isinstance(budget, dict) or set(budget) != set(BUDGET_KEYS):
        raise PolicyError(f"budget must have exactly {list(BUDGET_KEYS)}")
    for key in BUDGET_KEYS:
        value = budget[key]
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            raise PolicyError(f"budget.{key} must be a non-negative integer")
    for key in ("own_include_prefixes", "submodule_overrides"):
        values = policy[key]
        if not isinstance(values, list) or not all(
            isinstance(value, str) and value for value in values
        ):
            raise PolicyError(f"{key} must be a list of non-empty strings")
    if not policy["own_include_prefixes"]:
        raise PolicyError("own_include_prefixes must not be empty")
    return policy


def git(root, *args):
    return subprocess.run(
        ["git", *args],
        cwd=root,
        capture_output=True,
        check=True,
    ).stdout.decode("utf-8")


def tree(root, commit):
    """Map every path of the commit to its mode and object id."""
    result = {}
    for record in filter(None, git(root, "ls-tree", "-r", "-z", commit).split("\0")):
        meta, path = record.split("\t", 1)
        mode, _kind, sha = meta.split()
        result[path] = (mode, sha)
    return result


def changed_files(root, base):
    output = git(
        root, "diff", "--numstat", "--no-renames", "--ignore-submodules=all", "-z", base, "--"
    )
    for record in filter(None, output.split("\0")):
        added, _deleted, path = record.split("\t", 2)
        yield path, 0 if added == "-" else int(added)


def submodule_mismatches(base_tree, head_tree, overrides):
    return sorted(
        (path, head_tree[path][1], sha)
        for path, (mode, sha) in base_tree.items()
        if mode == GITLINK
        and path in head_tree
        and head_tree[path][1] != sha
        and path not in overrides
    )


def upstream_headers(root, policy):
    source_root = Path(root) / policy["source_root"]
    own = tuple(policy["own_include_prefixes"])
    result = set()
    for prefix in own:
        for path in sorted((source_root / prefix).rglob("*")):
            if path.suffix not in SOURCES or "tests" in path.parts or not path.is_file():
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for include in INCLUDE.findall(text):
                if not include.startswith(own) and (source_root / include).is_file():
                    result.add(include)
    return sorted(result)


def measure(root, policy, base_tree):
    metrics = dict.fromkeys(BUDGET_KEYS, 0)
    changed = []
    for path, added in changed_files(root, policy["base"]):
        if path not in base_tree or base_tree[path][0] == GITLINK:
            continue
        changed.append((added, path))
        metrics["upstream_files"] += 1
        metrics["upstream_added_lines"] += added
        if path.startswith(policy["source_root"]):
            metrics["source_files"] += 1
            metrics["source_added_lines"] += added
    metrics["upstream_headers"] = len(upstream_headers(root, policy))
    return metrics, sorted(changed, reverse=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=HERE.parents[1])
    parser.add_argument("--policy", default=DEFAULT_POLICY)
    parser.add_argument(
        "--list", action="store_true", help="list the changed upstream files by added lines"
    )
    parser.add_argument(
        "--headers", action="store_true", help="list the upstream headers that Nagram includes"
    )
    args = parser.parse_args(argv)
    try:
        policy = load_policy(args.policy)
    except PolicyError as error:
        print(error, file=sys.stderr)
        return 2
    base = policy["base"]
    try:
        base_tree = tree(args.root, base)
        metrics, changed = measure(args.root, policy, base_tree)
        mismatches = submodule_mismatches(
            base_tree, tree(args.root, "HEAD"), policy["submodule_overrides"]
        )
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        detail = getattr(error, "stderr", b"") or b""
        print(
            f"cannot compare with upstream base {base}: {error}\n"
            f"{detail.decode('utf-8', errors='replace')}"
            f"Fetch it with 'git fetch origin {base}', or record the new base "
            "in the policy after an upstream sync.",
            file=sys.stderr,
        )
        return 2
    exceeded = []
    slack = []
    for key in BUDGET_KEYS:
        limit = policy["budget"][key]
        marker = "over budget" if metrics[key] > limit else "ok"
        print(f"{key:<22} {metrics[key]:>6} / {limit:<6} {marker}")
        if metrics[key] > limit:
            exceeded.append(key)
        elif metrics[key] < limit:
            slack.append(key)
    if args.list:
        for added, path in changed:
            print(f"{added:>6} {path}")
    if args.headers:
        for header in upstream_headers(args.root, policy):
            print(f"upstream header: {header}")
    for path, current, upstream in mismatches:
        print(
            f"Submodule {path} is at {current[:10]} but the upstream base has "
            f"{upstream[:10]}; restore it or list it in submodule_overrides."
        )
    if exceeded:
        print(
            f"Upstream intrusion over budget: {', '.join(exceeded)}. Move the "
            "logic into nagram/ or, when the change is justified, raise the "
            "budget in the same commit and say why."
        )
    elif slack:
        print(f"Below budget, lower it in the policy: {', '.join(slack)}.")
    return 1 if (exceeded or mismatches) else 0


if __name__ == "__main__":
    sys.exit(main())
