import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest

from ricochet_lab.service import LabHTTPServer

ROOT = Path(__file__).resolve().parents[1]


class CliTests(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run([sys.executable, "-m", "ricochet_lab", *map(str, args)],
                              cwd=ROOT, capture_output=True, text=True, timeout=10)

    def test_demo_crosses_portable_layers(self):
        result = self.run_cli("demo")
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        self.assertEqual(len(data["review"]["findings"]), 3)
        self.assertEqual(data["tampered_message_rejected"], "mac")
        self.assertTrue(data["replayed_message_rejected"])
        self.assertTrue(data["integrity_before"]["ok"])
        self.assertFalse(data["integrity_after"]["ok"])
        self.assertEqual(data["visible_synthetic_entities"], ["observer", "near"])
        self.assertEqual(data["journal_records_verified_with_checkpoint"], 1)

    def test_key_creation_and_manifest_verification(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            key = root / "lab.key"
            self.assertEqual(self.run_cli("keygen", key).returncode, 0)
            original = key.read_bytes()
            self.assertEqual(len(original), 32)
            self.assertEqual(self.run_cli("keygen", key).returncode, 2)
            self.assertEqual(key.read_bytes(), original)
            (root / "asset").write_text("before", encoding="utf-8")
            measured = self.run_cli("manifest", root, "asset")
            self.assertEqual(measured.returncode, 0, measured.stderr)
            manifest = root / "manifest.json"
            manifest.write_text(measured.stdout, encoding="utf-8")
            self.assertEqual(self.run_cli("verify", root, manifest).returncode, 0)
            (root / "asset").write_text("after", encoding="utf-8")
            self.assertEqual(self.run_cli("verify", root, manifest).returncode, 2)

    def test_submit_over_real_loopback_socket(self):
        with tempfile.TemporaryDirectory() as directory:
            key = Path(directory) / "lab.key"
            key.write_bytes(b"T" * 32)
            with LabHTTPServer(key.read_bytes()) as server:
                thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.01})
                thread.start()
                try:
                    result = self.run_cli("submit", "--key", key, "--events",
                                          ROOT / "examples/synthetic-events.json", "--port", server.server_port)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(len(json.loads(result.stdout)["results"][0]["findings"]), 3)
                finally:
                    server.shutdown()
                    thread.join(timeout=2)


if __name__ == "__main__":
    unittest.main()
