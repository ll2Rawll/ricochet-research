import json
from pathlib import Path
import tempfile
import unittest

from ricochet_lab.telemetry import SCHEMA, read_kernel_log


def records():
    base = {"schema": SCHEMA, "session_id": 1}
    return [
        dict(base, type="session", target_process_id=1000),
        dict(base, type="batch", event_count=1, dropped_events=0, next_sequence=2),
        dict(base, type="event", sequence=1, time_100ns=133000000000000000,
             process_id=1000, thread_id=0, related_process_id=0, image_size=0, kind=1, flags=0),
        dict(base, type="stop"),
    ]


class TelemetryTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "collector.jsonl"

    def read(self, rows):
        self.path.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")
        return read_kernel_log(self.path)

    def test_preserves_controls_and_never_claims_authenticity(self):
        result = self.read(records())
        self.assertEqual(result["summary"]["native_events"], 1)
        self.assertEqual(result["summary"]["authenticity"], "not_established")
        self.assertEqual(len(result["events"]), 4)
        self.assertTrue(all(event["kind"] == "kernel_observation" for event in result["events"]))

    def test_loss_records_survive_import(self):
        rows = records()
        rows[1].update(dropped_events=5, next_sequence=7)
        self.assertEqual(self.read(rows)["summary"]["dropped_events"], 5)

    def test_rejects_truncation_mismatch_order_and_unknown_fields(self):
        cases = [records()[:-1], records() + [records()[3]], records()[1:]]
        for path, value in [((1, "event_count"), 2), ((2, "session_id"), 2),
                            ((2, "sequence"), 2), ((2, "flags"), 1),
                            ((2, "kind"), 99), ((2, "process_id"), True),
                            ((2, "time_100ns"), 2**64), ((0, "extra"), 1)]:
            rows = records()
            rows[path[0]][path[1]] = value
            cases.append(rows)
        for rows in cases:
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                self.read(rows)

    def test_rejects_duplicate_json_and_incomplete_final_line(self):
        self.read(records())
        original = self.path.read_bytes()
        for changed in [original[:-1], original.replace(b'"session_id": 1', b'"session_id": 1, "session_id": 2', 1), b"x" * 4097]:
            self.path.write_bytes(changed)
            with self.assertRaises(ValueError):
                read_kernel_log(self.path)


if __name__ == "__main__":
    unittest.main()
