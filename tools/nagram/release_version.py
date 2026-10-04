#!/usr/bin/env python3
"""Print the tag of the Nagram release in the tree, or check a tag against it.

A release is <upstream version>.<revision>: Telegram/build/version holds the
upstream version, Telegram/build/nagram_version the Nagram revision and the
channel of the latest release. Its tag is v<release>, with -beta appended on
the beta channel.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = Path("Telegram/build/version")
NAGRAM = Path("Telegram/build/nagram_version")
# The three-part form, so that 7.3 gives 7.3.0.1 and not the upstream 7.3.1.
UPSTREAM_VERSION = re.compile(r"^AppVersionStr +(\d+\.\d+\.\d+)$", re.MULTILINE)
# At most 999: update feeds carry upstream version * 1000 + revision.
REVISION = re.compile(r"NagramRevision +([1-9][0-9]{0,2})")
CHANNEL = re.compile(r"NagramChannel +(stable|beta)")


class VersionError(Exception):
    pass


def read(path):
    try:
        return path.read_text(encoding="utf-8")
    except OSError as error:
        raise VersionError(f"cannot read {path}: {error}") from error


def upstream_version(path):
    found = UPSTREAM_VERSION.findall(read(path))
    if len(found) != 1:
        raise VersionError(f"{path} must have one 'AppVersionStr <major>.<minor>.<patch>' line")
    return found[0]


def nagram_version(path):
    """The revision and the channel; any other line is an error."""
    revision = channel = None
    for line in filter(None, read(path).splitlines()):
        if (match := REVISION.fullmatch(line)) and revision is None:
            revision = match.group(1)
        elif (match := CHANNEL.fullmatch(line)) and channel is None:
            channel = match.group(1)
        else:
            raise VersionError(f"bad line in {path}: {line!r}")
    if revision is None or channel is None:
        raise VersionError(f"{path} must set NagramRevision and NagramChannel")
    return revision, channel


def release_tag(root):
    upstream = upstream_version(root / UPSTREAM)
    revision, channel = nagram_version(root / NAGRAM)
    return f"v{upstream}.{revision}" + ("-beta" if channel == "beta" else "")


def update_version(root):
    """The upstream AppVersion number, the revision and the channel."""
    major, minor, patch = map(int, upstream_version(root / UPSTREAM).split("."))
    revision, channel = nagram_version(root / NAGRAM)
    return major * 1000000 + minor * 1000 + patch, int(revision), channel


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("tag", nargs="?", help="fail unless this is the tag of the release")
    parser.add_argument("--version", action="store_true", help="print the version, like 7.2.10.3")
    parser.add_argument("--root", default=ROOT)
    args = parser.parse_args(argv)
    try:
        expected = release_tag(Path(args.root))
    except VersionError as error:
        print(error, file=sys.stderr)
        return 2
    if args.version:
        print(expected[1:].removesuffix("-beta"))
    elif args.tag is None:
        print(expected)
    elif args.tag != expected:
        print(
            f"Tag {args.tag} does not match the version files, which describe {expected}. "
            f"Update {NAGRAM} in the tagged commit, or tag it {expected}.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
