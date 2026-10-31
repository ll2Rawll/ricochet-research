import copy
import hashlib
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from ricochet_lab.integrity import IntegrityError, build_manifest, verify_manifest


class IntegrityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "assets").mkdir()
        (self.root / "assets/map.txt").write_bytes(b"map-v1")
        (self.root / "settings.json").write_bytes(b"{}")

    def manifest(self):
        return build_manifest(self.root, ["settings.json", "assets/map.txt"])

    def test_known_hash_and_deterministic_order(self):
        manifest = self.manifest()
        self.assertEqual(manifest["files"][0], {
            "path": "assets/map.txt", "sha256": hashlib.sha256(b"map-v1").hexdigest(), "size": 6,
        })
        self.assertTrue(verify_manifest(self.root, manifest)["ok"])
        self.assertEqual(manifest, build_manifest(self.root, ["assets/map.txt", "settings.json"]))

    def test_modified_missing_and_unlisted_files(self):
        manifest = self.manifest()
        (self.root / "assets/map.txt").write_bytes(b"map-v2")
        (self.root / "settings.json").unlink()
        (self.root / "unlisted.txt").write_bytes(b"outside explicit scope")
        report = verify_manifest(self.root, manifest)
        self.assertFalse(report["ok"])
        self.assertEqual(report["modified"], ["assets/map.txt"])
        self.assertEqual(report["missing"], ["settings.json"])
        self.assertEqual(report["checked"], 1)
        self.assertEqual(report["errors"], [])

    def test_unsafe_paths_rejected_before_read(self):
        for path in ("../secret", "/etc/passwd", "a/../b", "./x", "a//b", "a/", "C:/x", "a\\b", "a\0b", "", "\ud800",
                     ".. /secret", "name.", "NUL", "con.txt", "assets/COM1.log"):
            with self.subTest(path=repr(path)), self.assertRaises(IntegrityError):
                build_manifest(self.root, [path])

    def test_manifest_schema_validation(self):
        manifest = self.manifest()
        candidates = [None, {}, {"version": True, "files": []}, {"version": 2, "files": []},
                      {"version": 1, "files": [], "extra": 0}, {"version": 1, "files": "x"}]
        for field, bad_value in (("size", True), ("size", -1), ("sha256", "A" * 64), ("path", "../x")):
            bad = copy.deepcopy(manifest)
            bad["files"][0][field] = bad_value
            candidates.append(bad)
        duplicate = copy.deepcopy(manifest)
        duplicate["files"].append(duplicate["files"][0])
        candidates.append(duplicate)
        for bad in candidates:
            with self.subTest(bad=bad), self.assertRaises(IntegrityError):
                verify_manifest(self.root, bad)

    def test_duplicate_paths_and_limits(self):
        with self.assertRaises(IntegrityError):
            build_manifest(self.root, ["settings.json", "settings.json"])
        with patch("ricochet_lab.integrity.MAX_FILES", 1), self.assertRaises(IntegrityError):
            self.manifest()
        with patch("ricochet_lab.integrity.MAX_FILE_BYTES", 4), self.assertRaises(IntegrityError):
            build_manifest(self.root, ["assets/map.txt"])
        with patch("ricochet_lab.integrity.MAX_TOTAL_BYTES", 7), self.assertRaises(IntegrityError):
            self.manifest()

    def test_actual_reads_share_total_budget(self):
        manifest = self.manifest()
        (self.root / "assets/map.txt").write_bytes(b"x" * 8)
        (self.root / "settings.json").write_bytes(b"y" * 8)
        # Expected sizes fit, but measuring both modified files does not.
        with patch("ricochet_lab.integrity.MAX_TOTAL_BYTES", 9):
            report = verify_manifest(self.root, manifest)
        self.assertEqual(report["modified"], ["assets/map.txt"])
        self.assertEqual(report["errors"][0]["path"], "settings.json")
        self.assertFalse(report["ok"])

    def test_directory_and_missing_root(self):
        with self.assertRaises(IntegrityError):
            build_manifest(self.root, ["assets"])
        with self.assertRaises(IntegrityError):
            build_manifest(self.root / "missing", [])

    @unittest.skipUnless(hasattr(os, "symlink"), "symlink unavailable")
    def test_symlink_file_and_directory_rejected(self):
        try:
            (self.root / "link.txt").symlink_to(self.root / "settings.json")
            (self.root / "linked-assets").symlink_to(self.root / "assets", target_is_directory=True)
        except OSError as exc:
            self.skipTest(f"symlinks not permitted: {exc}")
        for path in ("link.txt", "linked-assets/map.txt"):
            with self.subTest(path=path), self.assertRaises(IntegrityError):
                build_manifest(self.root, [path])

    @unittest.skipUnless(hasattr(os, "mkfifo"), "FIFO unavailable")
    def test_special_files_do_not_block(self):
        os.mkfifo(self.root / "pipe")
        with self.assertRaises(IntegrityError):
            build_manifest(self.root, ["pipe"])

    def test_portable_fallback_normal_and_symlink(self):
        with patch("ricochet_lab.integrity.os.supports_dir_fd", set()):
            self.assertTrue(verify_manifest(self.root, self.manifest())["ok"])
            try:
                (self.root / "alias").symlink_to(self.root / "settings.json")
            except OSError:
                return
            with self.assertRaises(IntegrityError):
                build_manifest(self.root, ["alias"])


if __name__ == "__main__":
    unittest.main()
