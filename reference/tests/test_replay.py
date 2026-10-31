import json
import math
import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest.mock import patch

from ricochet_lab.replay import (
    JournalError, MAX_RECORD_BYTES, read_journal, write_journal,
)


class ReplayTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "audit.jsonl"
        self.key = b"k" * 32
        self.records = [{"kind": "movement", "position": [0, 1.25, 2]},
                        {"kind": "equipment", "item": "starter", "label": "test é"}]

    def write(self):
        return write_journal(self.path, self.records, self.key)

    def test_roundtrip_empty_and_permissions(self):
        checkpoint = self.write()
        self.assertEqual(checkpoint["count"], 2)
        self.assertEqual(read_journal(self.path, self.key, checkpoint), self.records)
        self.assertEqual(read_journal(self.path, self.key), self.records)
        if os.name == "posix":
            self.assertEqual(stat.S_IMODE(self.path.stat().st_mode), 0o600)
        empty = self.path.with_name("empty.jsonl")
        checkpoint = write_journal(empty, [], self.key)
        self.assertEqual(read_journal(empty, self.key, checkpoint), [])

    def test_refuses_existing_file_and_symlink(self):
        self.write()
        original = self.path.read_bytes()
        with self.assertRaises(FileExistsError):
            write_journal(self.path, [], self.key)
        self.assertEqual(self.path.read_bytes(), original)
        if hasattr(os, "symlink"):
            link = self.path.with_name("link.jsonl")
            try:
                link.symlink_to(self.path)
            except OSError:
                return
            with self.assertRaises(FileExistsError):
                write_journal(link, [], self.key)
            self.assertEqual(self.path.read_bytes(), original)

    def test_tamper_reorder_wrong_key_and_index(self):
        checkpoint = self.write()
        original = self.path.read_bytes()
        lines = original.splitlines(keepends=True)
        variants = [original.replace(b"starter", b"altered"),
                    b"".join(reversed(lines)),
                    original.replace(b'"index":0', b'"index":1', 1),
                    original.replace(b'"version":1', b'"version":true', 1),
                    original.replace(b'"mac":"', b'"mac":"x', 1)]
        for variant in variants:
            self.path.write_bytes(variant)
            with self.subTest(variant=variant[:100]), self.assertRaises(JournalError):
                read_journal(self.path, self.key, checkpoint)
        self.path.write_bytes(original)
        with self.assertRaises(JournalError):
            read_journal(self.path, b"w" * 32, checkpoint)

    def test_truncation_requires_trusted_checkpoint(self):
        checkpoint = self.write()
        first = self.path.read_bytes().splitlines(keepends=True)[0]
        self.path.write_bytes(first)
        self.assertEqual(read_journal(self.path, self.key), self.records[:1])
        with self.assertRaises(JournalError):
            read_journal(self.path, self.key, checkpoint)
        self.path.write_bytes(b"")
        with self.assertRaises(JournalError):
            read_journal(self.path, self.key, checkpoint)

    def test_partial_line_and_duplicate_keys_are_rejected(self):
        self.write()
        original = self.path.read_bytes()
        variants = [original.rstrip(b"\n"), original + b"\n",
                    original.replace(b'"version":1', b'"version":1,"version":1', 1),
                    original.replace(b'"kind":"movement"',
                                     b'"kind":"movement","kind":"movement"', 1),
                    original.replace(b"1.25", b"NaN"),
                    original.replace(b"1.25", b"1e9999"),
                    b"\xff\n", b"[]\n", b"{\n"]
        for variant in variants:
            self.path.write_bytes(variant)
            with self.subTest(variant=variant[:100]), self.assertRaises(JournalError):
                read_journal(self.path, self.key)

    def test_canonicalization_allows_object_key_reordering(self):
        checkpoint = self.write()
        reordered = []
        for raw in self.path.read_text().splitlines():
            value = json.loads(raw)
            reordered.append(json.dumps(dict(reversed(list(value.items())))))
        self.path.write_text("\n".join(reordered) + "\n")
        self.assertEqual(read_journal(self.path, self.key, checkpoint), self.records)

    def test_invalid_checkpoints(self):
        checkpoint = self.write()
        for invalid in (
            {}, {**checkpoint, "extra": 1}, {**checkpoint, "version": True},
            {**checkpoint, "count": True}, {**checkpoint, "count": -1},
            {**checkpoint, "final_mac": "z" * 64},
            {**checkpoint, "final_mac": "0" * 64},
        ):
            with self.subTest(invalid=invalid), self.assertRaises(JournalError):
                read_journal(self.path, self.key, invalid)

    def test_writer_rejects_non_json_and_excessive_data(self):
        for index, record in enumerate((
            [], {"number": math.nan}, {"number": math.inf},
            {"integer": 2**63}, {1: "numeric key"}, {"tuple": (1,)},
            {"text": "\ud800"}, {"text": "x" * MAX_RECORD_BYTES},
            {"items": list(range(5000))},
        )):
            with self.subTest(record_type=type(record)), self.assertRaises(JournalError):
                write_journal(self.path.with_name(f"bad-{index}.jsonl"), [record], self.key)
        deep = {}
        for _ in range(20):
            deep = {"child": deep}
        with self.assertRaises(JournalError):
            write_journal(self.path.with_name("deep.jsonl"), [deep], self.key)
        with self.assertRaises(JournalError):
            write_journal(self.path, [], b"short")
        self.assertFalse(self.path.exists())

    def test_reader_and_writer_resource_limits(self):
        self.path.write_bytes(b"x" * (MAX_RECORD_BYTES + 1) + b"\n")
        with self.assertRaises(JournalError):
            read_journal(self.path, self.key)
        self.path.unlink()
        with patch("ricochet_lab.replay.MAX_RECORDS", 1), self.assertRaises(JournalError):
            self.write()
        self.path.unlink()
        with patch("ricochet_lab.replay.MAX_TOTAL_BYTES", 100), self.assertRaises(JournalError):
            self.write()
        self.path.unlink()
        self.write()
        with patch("ricochet_lab.replay.MAX_RECORDS", 1), self.assertRaises(JournalError):
            read_journal(self.path, self.key)
        with patch("ricochet_lab.replay.MAX_TOTAL_BYTES", 100), self.assertRaises(JournalError):
            read_journal(self.path, self.key)


if __name__ == "__main__":
    unittest.main()
