#!/usr/bin/env python3
"""Add the update packages of a release to the update feed.

The client reads one JSON file, nagram-updates.json of the 'updates' release:
  { "<platform>": { "<channel>": { "released": <number>, "link": "/<tag>/<package>" } } }
The number is the upstream AppVersion * 1000 + the Nagram revision, the link
is relative to the release download address. Entries of other platforms and
of the other channel are kept, and an entry is never replaced by an older one.
"""

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import release_version

PACKAGE = re.compile(r"td-update-(win|mac|linux)-(x86|x64|arm)-(\d+)(-beta)?")
# The names of Platform::AutoUpdateKey() for each packed target.
PLATFORMS = {
    ("win", "x86"): "win",
    ("win", "x64"): "win64",
    ("win", "arm"): "winarm",
    ("mac", "x64"): "mac",
    ("mac", "arm"): "armac",
    ("linux", "x64"): "linux",
}


class FeedError(Exception):
    pass


def load(path):
    if not path.exists() or not path.read_bytes().strip():
        return {}
    try:
        feed = json.loads(path.read_bytes())
    except ValueError as error:
        raise FeedError(f"{path} is not JSON: {error}") from error
    if not isinstance(feed, dict) or not all(isinstance(v, dict) for v in feed.values()):
        raise FeedError(f"{path} must map platforms to channels")
    return feed


def add(feed, names, tag, version, revision, channel):
    """Add the named packages of the release; returns the lines to report."""
    report = []
    released = version * 1000 + revision
    for name in sorted(names):
        match = PACKAGE.fullmatch(name)
        if not match:
            raise FeedError(f"{name} is not an update package name")
        system, arch, base, beta = match.groups()
        platform = PLATFORMS.get((system, arch))
        if platform is None:
            raise FeedError(f"{name} has no platform in the feed")
        elif int(base) != version or bool(beta) != (channel == "beta"):
            raise FeedError(f"{name} does not belong to {tag} ({version}, {channel})")
        entry = feed.setdefault(platform, {}).get(channel)
        current = entry.get("released", 0) if isinstance(entry, dict) else 0
        if isinstance(current, int) and current > released:
            report.append(f"{platform} {channel}: kept {current}, newer than {released}")
            continue
        feed[platform][channel] = {"released": released, "link": f"/{tag}/{name}"}
        report.append(f"{platform} {channel}: {released} /{tag}/{name}")
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--feed", required=True, help="the feed file, updated in place")
    parser.add_argument("--tag", required=True, help="the tag the packages are published under")
    parser.add_argument("--root", default=release_version.ROOT)
    parser.add_argument("packages", nargs="+", help="update package files or names")
    args = parser.parse_args(argv)
    try:
        root = Path(args.root)
        expected = release_version.release_tag(root)
        if args.tag != expected:
            raise FeedError(f"the version files describe {expected}, not {args.tag}")
        feed = load(Path(args.feed))
        names = [Path(package).name for package in args.packages]
        report = add(feed, names, args.tag, *release_version.update_version(root))
    except (FeedError, release_version.VersionError) as error:
        print(error, file=sys.stderr)
        return 1
    Path(args.feed).write_text(json.dumps(feed, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("\n".join(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
