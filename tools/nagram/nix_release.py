#!/usr/bin/env python3
"""Point the Nix package at a released Linux archive.

flake.nix installs the archive that packaging/nix/release.json names, so the
file is rewritten after every stable release: the version, taken from the
archive name, and the hash of the archive. An older archive does not replace
a newer entry.
"""

import argparse
import base64
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RELEASE = Path("packaging/nix/release.json")
# Stable releases only: the archive of a beta has -beta after the version.
ARCHIVE = re.compile(r"Nagram-(\d+\.\d+\.\d+\.\d+)-linux-x86_64\.tar\.xz")


class ReleaseError(Exception):
    pass


def number(version):
    return tuple(int(part) for part in version.split("."))


def entry(archive):
    match = ARCHIVE.fullmatch(archive.name)
    if not match:
        raise ReleaseError(f"{archive.name} is not the Linux archive of a stable release")
    try:
        digest = hashlib.sha256(archive.read_bytes()).digest()
    except OSError as error:
        raise ReleaseError(f"cannot read {archive}: {error}") from error
    return {"version": match.group(1), "hash": "sha256-" + base64.b64encode(digest).decode()}


def current(path):
    if not path.exists():
        return None
    try:
        data = json.loads(path.read_bytes())
        number(data["version"])
    except (ValueError, KeyError, TypeError, AttributeError) as error:
        raise ReleaseError(f"{path} is not a release entry: {error}") from error
    return data


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--archive", required=True, help="the released Linux archive")
    parser.add_argument("--out", default=ROOT / RELEASE, help="the release file to write")
    args = parser.parse_args(argv)
    out = Path(args.out)
    try:
        new = entry(Path(args.archive))
        old = current(out)
    except ReleaseError as error:
        print(error, file=sys.stderr)
        return 1
    if old and number(old["version"]) > number(new["version"]):
        print(f"{out} stays at {old['version']}, newer than {new['version']}")
        return 0
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(new, indent=2) + "\n", encoding="utf-8")
    print(f"{out} now names {new['version']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
