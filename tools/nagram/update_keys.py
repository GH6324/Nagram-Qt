#!/usr/bin/env python3
"""Create or rotate the keys that Nagram update packages are verified with.

The client pins root-public.pem and trusts the manifest that this root key
signed; the manifest names the key that signs update packages. 'init' makes
both keys and the first manifest, 'rotate' replaces the package key and
revokes the earlier ones. Private keys go to a directory outside the
repository and are never printed; the public files go to
Telegram/Resources/nagram/update.
"""

import argparse
import base64
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PUBLIC = Path("Telegram/Resources/nagram/update")
ROOT_PRIVATE = "root-private.pem"
ROOT_PUBLIC = "root-public.pem"
MANIFEST = "manifest.min.json"
SIGNATURE = "manifest.sig"
# The signed bytes must reach every checkout unchanged.
ATTRIBUTES = "*.min.json -text\n*.sig binary\n*.pem -text\n"
# The upstream test of the embedded manifest expects all four channels.
CHANNELS = ("stable", "beta", "canary-public", "canary-private")
KEY_ID = re.compile(r"[a-z0-9][a-z0-9-]{0,63}")


class KeysError(Exception):
    pass


def openssl(*arguments):
    try:
        result = subprocess.run(["openssl", *arguments], capture_output=True, check=False)
    except OSError as error:
        raise KeysError(f"cannot run openssl: {error}") from error
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise KeysError(f"openssl {arguments[0]} failed: {detail}")
    return result.stdout


def ed25519_available():
    try:
        openssl("genpkey", "-algorithm", "ed25519")
    except KeysError:
        return False
    return True


def new_private_key(path):
    pem = openssl("genpkey", "-algorithm", "ed25519")
    with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as stream:
        stream.write(pem)


def public_pem(private):
    return openssl("pkey", "-in", str(private), "-pubout")


def public_raw(private):
    return openssl("pkey", "-in", str(private), "-pubout", "-outform", "DER")[-32:]


def verify(directory):
    """Check the manifest signature against the root public key, as the client does."""
    try:
        openssl(
            *("pkeyutl", "-verify", "-pubin", "-inkey", str(directory / ROOT_PUBLIC)),
            *("-rawin", "-in", str(directory / MANIFEST)),
            *("-sigfile", str(directory / SIGNATURE)),
        )
    except KeysError as error:
        raise KeysError(f"the manifest signature does not match {ROOT_PUBLIC}: {error}") from error


def sign(root_private, root_public, manifest):
    """The root signature of the manifest, checked against the root public key."""
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        (temp / MANIFEST).write_bytes(manifest)
        signature = openssl(
            "pkeyutl", "-sign", "-inkey", str(root_private), "-rawin", "-in", str(temp / MANIFEST)
        )
        (temp / SIGNATURE).write_bytes(signature)
        (temp / ROOT_PUBLIC).write_bytes(root_public)
        verify(temp)
    return signature


def manifest_bytes(version, key_id, key, revoked):
    encoded = base64.urlsafe_b64encode(key).rstrip(b"=").decode()
    manifest = {
        "format": 1,
        "manifest_version": version,
        "issued": int(time.time()),
        "keys": [{"id": key_id, "alg": "Ed25519", "x": encoded}],
        "channels": {channel: [[key_id]] for channel in CHANNELS},
        "revoked": revoked,
    }
    return json.dumps(manifest, separators=(",", ":")).encode()


def prepare(root, private, key_id):
    """The public and private directories and the path of the new package key."""
    if not ed25519_available():
        raise KeysError(
            "This openssl cannot make Ed25519 keys (macOS ships LibreSSL). Run with OpenSSL 3, "
            "for example under: nix shell nixpkgs#openssl"
        )
    if not KEY_ID.fullmatch(key_id):
        raise KeysError(f"bad key id {key_id!r}: use lowercase letters, digits and dashes")
    root, private = Path(root).resolve(), Path(private).resolve()
    if private == root or root in private.parents:
        raise KeysError(f"{private} is inside the repository; keep private keys outside it")
    package = private / f"{key_id}-private.pem"
    if package.exists():
        raise KeysError(f"{package} already exists; a private key is never overwritten")
    return root / PUBLIC, private, package


def init(root, private, key_id):
    public, private, package = prepare(root, private, key_id)
    if (public / ROOT_PUBLIC).exists():
        raise KeysError(f"{public / ROOT_PUBLIC} already exists; rotate replaces the package key")
    if (private / ROOT_PRIVATE).exists():
        raise KeysError(f"{private / ROOT_PRIVATE} already exists; it is never overwritten")
    private.mkdir(mode=0o700, parents=True, exist_ok=True)
    new_private_key(private / ROOT_PRIVATE)
    new_private_key(package)
    root_public = public_pem(private / ROOT_PRIVATE)
    manifest = manifest_bytes(1, key_id, public_raw(package), [])
    signature = sign(private / ROOT_PRIVATE, root_public, manifest)
    public.mkdir(parents=True, exist_ok=True)
    (public / ".gitattributes").write_text(ATTRIBUTES, encoding="utf-8")
    (public / ROOT_PUBLIC).write_bytes(root_public)
    (public / MANIFEST).write_bytes(manifest)
    (public / SIGNATURE).write_bytes(signature)
    print(f"Wrote the trust files to {public}; commit them.")
    print(f"Private keys, in {private}:")
    print(f"  {ROOT_PRIVATE}: keep it offline, it is needed only to rotate the package key")
    print(f"  {package.name}: signs update packages, give it to the release workflow as a secret")


def rotate(root, private, key_id):
    public, private, package = prepare(root, private, key_id)
    if not (private / ROOT_PRIVATE).is_file():
        raise KeysError(f"{private / ROOT_PRIVATE} is missing; rotating needs the root private key")
    try:
        verify(public)
        current = json.loads((public / MANIFEST).read_bytes())
        earlier = [key["id"] for key in current["keys"]]
        revoked = current["revoked"] + [id for id in earlier if id not in current["revoked"]]
        version = current["manifest_version"] + 1
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise KeysError(f"cannot read the committed manifest in {public}: {error}") from error
    if key_id in revoked:
        raise KeysError(f"key id {key_id} was already used; choose a new one")
    new_private_key(package)
    try:
        manifest = manifest_bytes(version, key_id, public_raw(package), revoked)
        signature = sign(private / ROOT_PRIVATE, (public / ROOT_PUBLIC).read_bytes(), manifest)
    except KeysError:
        package.unlink()
        raise
    (public / MANIFEST).write_bytes(manifest)
    (public / SIGNATURE).write_bytes(signature)
    print(f"Wrote manifest version {version} to {public}; commit it.")
    print(f"Revoked: {', '.join(revoked)}.")
    print(f"The new package key is {package}; replace the release workflow secret with it.")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("action", choices=("init", "rotate"))
    parser.add_argument("--private", required=True, help="directory of the private keys")
    parser.add_argument("--key-id", help="id of the new package key, like release-2026a")
    parser.add_argument("--root", default=ROOT)
    args = parser.parse_args(argv)
    try:
        if args.action == "init":
            init(args.root, args.private, args.key_id or f"release-{time.gmtime().tm_year}a")
        elif not args.key_id:
            parser.error("rotate needs --key-id")
        else:
            rotate(args.root, args.private, args.key_id)
    except KeysError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
