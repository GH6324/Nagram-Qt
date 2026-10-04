#!/usr/bin/env python3
"""Tests of update_keys.py: python3 tools/nagram/test_update_keys.py"""
import contextlib
import io
import json
import stat
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import update_keys as keys


@unittest.skipUnless(keys.ed25519_available(), "needs an openssl with Ed25519")
class UpdateKeysTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.root = Path(self._temp.name).resolve() / "repository"
        self.private = Path(self._temp.name).resolve() / "private"
        self.public = self.root / keys.PUBLIC
        self.root.mkdir()

    def tearDown(self):
        self._temp.cleanup()

    def run_main(self, action, *arguments, private=None):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = keys.main(
                [action, "--root", str(self.root), "--private", str(private or self.private)]
                + list(arguments)
            )
        return code, out.getvalue(), err.getvalue()

    def manifest(self):
        return json.loads((self.public / keys.MANIFEST).read_bytes())

    def test_init_writes_a_manifest_the_root_key_signed(self):
        code, out, err = self.run_main("init", "--key-id", "release-test")
        self.assertEqual((code, err), (0, ""))
        manifest = self.manifest()
        self.assertEqual(manifest["format"], 1)
        self.assertEqual(manifest["manifest_version"], 1)
        self.assertEqual(manifest["revoked"], [])
        self.assertEqual([key["id"] for key in manifest["keys"]], ["release-test"])
        self.assertEqual(manifest["keys"][0]["alg"], "Ed25519")
        self.assertEqual(len(manifest["keys"][0]["x"]), 43)
        self.assertEqual(
            manifest["channels"],
            {channel: [["release-test"]] for channel in keys.CHANNELS},
        )
        raw = (self.public / keys.MANIFEST).read_bytes()
        self.assertEqual(raw, json.dumps(manifest, separators=(",", ":")).encode())
        self.assertEqual(len((self.public / keys.SIGNATURE).read_bytes()), 64)
        self.assertEqual((self.public / ".gitattributes").read_text(), keys.ATTRIBUTES)
        keys.verify(self.public)

    def test_init_keeps_private_keys_out_of_the_output_and_readable_only_by_the_owner(self):
        _, out, _ = self.run_main("init", "--key-id", "release-test")
        for name in (keys.ROOT_PRIVATE, "release-test-private.pem"):
            path = self.private / name
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
            self.assertIn("PRIVATE KEY", path.read_text())
        self.assertNotIn("PRIVATE KEY", out)
        self.assertFalse(list(self.root.rglob("*private*")))

    def test_verify_rejects_a_changed_manifest(self):
        self.run_main("init", "--key-id", "release-test")
        with open(self.public / keys.MANIFEST, "ab") as stream:
            stream.write(b" ")
        with self.assertRaises(keys.KeysError):
            keys.verify(self.public)

    def test_init_refuses_to_replace_an_existing_root(self):
        self.run_main("init", "--key-id", "release-test")
        before = (self.public / keys.ROOT_PUBLIC).read_bytes()
        code, _, err = self.run_main("init", "--key-id", "release-other")
        self.assertEqual(code, 1)
        self.assertIn("already exists", err)
        self.assertEqual((self.public / keys.ROOT_PUBLIC).read_bytes(), before)
        self.assertFalse((self.private / "release-other-private.pem").exists())

    def test_private_keys_must_stay_outside_the_repository(self):
        for inside in (self.root, self.root / "keys"):
            code, _, err = self.run_main("init", private=inside)
            self.assertEqual(code, 1)
            self.assertIn("inside the repository", err)
        self.assertFalse(self.public.exists())

    def test_rejects_bad_key_ids(self):
        for key_id in ("Release", "has space", "under_score", "x" * 65):
            code, _, err = self.run_main("init", "--key-id", key_id)
            self.assertEqual((code, "bad key id" in err), (1, True), key_id)
        self.assertFalse(self.public.exists())

    def test_rotate_replaces_the_package_key_and_revokes_the_earlier_ones(self):
        self.run_main("init", "--key-id", "release-a")
        root = (self.public / keys.ROOT_PUBLIC).read_bytes()
        self.assertEqual(self.run_main("rotate", "--key-id", "release-b")[0], 0)
        self.assertEqual(self.run_main("rotate", "--key-id", "release-c")[0], 0)
        manifest = self.manifest()
        self.assertEqual(manifest["manifest_version"], 3)
        self.assertEqual([key["id"] for key in manifest["keys"]], ["release-c"])
        self.assertEqual(manifest["revoked"], ["release-a", "release-b"])
        self.assertEqual(manifest["channels"]["stable"], [["release-c"]])
        self.assertEqual((self.public / keys.ROOT_PUBLIC).read_bytes(), root)
        self.assertTrue((self.private / "release-c-private.pem").is_file())
        keys.verify(self.public)

    def test_rotate_refuses_a_used_key_id(self):
        self.run_main("init", "--key-id", "release-a")
        self.run_main("rotate", "--key-id", "release-b")
        (self.private / "release-a-private.pem").unlink()
        before = (self.public / keys.MANIFEST).read_bytes()
        code, _, err = self.run_main("rotate", "--key-id", "release-a")
        self.assertEqual(code, 1)
        self.assertIn("already used", err)
        self.assertEqual((self.public / keys.MANIFEST).read_bytes(), before)

    def test_rotate_refuses_a_root_key_that_is_not_the_pinned_one(self):
        self.run_main("init", "--key-id", "release-a")
        other = self.private.parent / "other"
        other.mkdir()
        keys.new_private_key(other / keys.ROOT_PRIVATE)
        before = (self.public / keys.MANIFEST).read_bytes()
        code, _, err = self.run_main("rotate", "--key-id", "release-b", private=other)
        self.assertEqual(code, 1)
        self.assertIn("does not match", err)
        self.assertEqual((self.public / keys.MANIFEST).read_bytes(), before)
        self.assertFalse((other / "release-b-private.pem").exists())

    def test_the_committed_trust_files_verify(self):
        public = keys.ROOT / keys.PUBLIC
        keys.verify(public)
        manifest = json.loads((public / keys.MANIFEST).read_bytes())
        ids = {key["id"] for key in manifest["keys"]} - set(manifest["revoked"])
        self.assertEqual(set(manifest["channels"]), set(keys.CHANNELS))
        for groups in manifest["channels"].values():
            self.assertTrue(all(ids.intersection(group) for group in groups), groups)

    def test_rotate_needs_the_root_private_key(self):
        self.run_main("init", "--key-id", "release-a")
        (self.private / keys.ROOT_PRIVATE).unlink()
        code, _, err = self.run_main("rotate", "--key-id", "release-b")
        self.assertEqual(code, 1)
        self.assertIn("root private key", err)


if __name__ == "__main__":
    unittest.main()
