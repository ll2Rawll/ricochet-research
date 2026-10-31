"""Behavioral tests for the lab protocol, with no network or Windows dependency."""

import copy
import json
import threading
import unittest
from concurrent.futures import ThreadPoolExecutor

from ricochet_lab.protocol import (
    MAX_ENVELOPE_BYTES,
    MAX_EVENTS,
    ProtocolError,
    SessionVerifier,
    canonical_json,
    loads_envelope,
    sign_batch,
)


SECRET = bytes(range(32))
EVENTS = [{"kind": "file_integrity", "payload": {"path": "assets/demo.bin", "ok": True}}]


class FakeClock:
    def __init__(self):
        self.now = 100.0

    def __call__(self):
        return self.now


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.clock = FakeClock()
        self.verifier = SessionVerifier(SECRET, clock=self.clock, challenge_ttl=10, session_ttl=30)

    def signed(self, session="demo", events=None):
        challenge = self.verifier.issue_challenge(session)
        return sign_batch(SECRET, challenge, challenge["next_sequence"], events or EVENTS)

    def assertRejected(self, code, operation):
        with self.assertRaises(ProtocolError) as caught:
            operation()
        self.assertEqual(caught.exception.code, code)

    def test_accepts_snapshot_and_next_sequence(self):
        envelope = self.signed()
        accepted = self.verifier.accept(envelope)
        self.assertEqual(accepted["sequence"], 0)
        self.assertNotIn("mac", accepted)
        accepted["events"][0]["payload"]["path"] = "changed"
        self.assertEqual(envelope["events"][0]["payload"]["path"], "assets/demo.bin")
        second = self.signed()
        self.assertEqual(second["sequence"], 1)
        self.assertNotEqual(second["challenge"], envelope["challenge"])
        self.assertEqual(self.verifier.accept(second)["sequence"], 1)

    def test_wire_round_trip_and_order_independence(self):
        envelope = self.signed(events=[{"kind": "measurement", "payload": {"z": 2.5, "a": "é"}}])
        raw = json.dumps(envelope, indent=2, ensure_ascii=True).encode()
        self.assertEqual(loads_envelope(raw), envelope)
        self.assertEqual(self.verifier.accept(raw)["events"], envelope["events"])
        self.assertEqual(canonical_json({"b": 2, "a": 1}), canonical_json({"a": 1, "b": 2}))

    def test_replay_is_rejected(self):
        envelope = self.signed()
        self.verifier.accept(envelope)
        self.assertRejected("challenge", lambda: self.verifier.accept(envelope))
        self.verifier.issue_challenge("demo")
        self.assertRejected("challenge", lambda: self.verifier.accept(envelope))

    def test_bad_mac_does_not_consume_challenge(self):
        envelope = self.signed()
        tampered = copy.deepcopy(envelope)
        tampered["events"][0]["payload"]["ok"] = False
        self.assertRejected("mac", lambda: self.verifier.accept(tampered))
        self.assertEqual(self.verifier.accept(envelope)["sequence"], 0)

    def test_wrong_key_does_not_consume_challenge(self):
        challenge = self.verifier.issue_challenge("demo")
        wrong = sign_batch(b"x" * 32, challenge, 0, EVENTS)
        self.assertRejected("mac", lambda: self.verifier.accept(wrong))
        self.verifier.accept(sign_batch(SECRET, challenge, 0, EVENTS))

    def test_wrong_session_challenge_is_rejected(self):
        one = self.verifier.issue_challenge("one")
        two = self.verifier.issue_challenge("two")
        one["session_id"] = two["session_id"]
        self.assertRejected("challenge", lambda: self.verifier.accept(sign_batch(SECRET, one, 0, EVENTS)))
        self.verifier.accept(sign_batch(SECRET, two, 0, EVENTS))

    def test_exact_expiry_and_refresh(self):
        expired = self.signed()
        self.clock.now += 10
        self.assertRejected("expired", lambda: self.verifier.accept(expired))
        refreshed = self.signed()
        self.assertNotEqual(refreshed["challenge"], expired["challenge"])
        self.assertEqual(refreshed["sequence"], 0)
        self.verifier.accept(refreshed)

    def test_repeated_request_does_not_extend_challenge(self):
        initial = self.verifier.issue_challenge("demo")
        self.clock.now += 5
        self.assertEqual(initial, self.verifier.issue_challenge("demo"))

    def test_wrong_sequence_rejected_without_consuming_nonce(self):
        challenge = self.verifier.issue_challenge("demo")
        forged_challenge = dict(challenge, next_sequence=1)
        wrong = sign_batch(SECRET, forged_challenge, 1, EVENTS)
        self.assertRejected("sequence", lambda: self.verifier.accept(wrong))
        self.verifier.accept(sign_batch(SECRET, challenge, 0, EVENTS))

    def test_atomic_acceptance_allows_one_concurrent_winner(self):
        envelope = self.signed()
        barrier = threading.Barrier(12)

        def attempt(_):
            barrier.wait()
            try:
                self.verifier.accept(envelope)
                return "accepted"
            except ProtocolError as exc:
                return exc.code

        with ThreadPoolExecutor(max_workers=12) as pool:
            outcomes = list(pool.map(attempt, range(12)))
        self.assertEqual(outcomes.count("accepted"), 1)
        self.assertEqual(outcomes.count("challenge"), 11)

    def test_capacity_and_idle_cleanup(self):
        verifier = SessionVerifier(SECRET, clock=self.clock, challenge_ttl=10, session_ttl=20, max_sessions=2)
        verifier.issue_challenge("one")
        verifier.issue_challenge("two")
        self.assertRejected("capacity", lambda: verifier.issue_challenge("three"))
        self.assertEqual(verifier.session_count, 2)
        self.clock.now += 20
        self.assertEqual(verifier.session_count, 0)
        verifier.issue_challenge("three")
        self.assertEqual(verifier.session_count, 1)

    def test_idle_session_cannot_replay_after_recreation(self):
        envelope = self.signed()
        self.clock.now += 30
        self.assertRejected("session", lambda: self.verifier.accept(envelope))
        fresh = self.signed()
        self.assertRejected("challenge", lambda: self.verifier.accept(envelope))
        self.verifier.accept(fresh)

    def test_active_ids_prunes_without_evicting_live_sessions(self):
        self.verifier.issue_challenge("old")
        self.clock.now += 10
        self.verifier.issue_challenge("new")
        snapshot = self.verifier.active_session_ids()
        self.assertEqual(snapshot, frozenset({"old", "new"}))
        self.clock.now += 20
        self.assertEqual(self.verifier.active_session_ids(), frozenset({"new"}))
        self.assertEqual(snapshot, frozenset({"old", "new"}))
        self.assertEqual(self.verifier.issue_challenge("new")["next_sequence"], 0)

    def test_close_invalidates_challenge_and_is_idempotent(self):
        envelope = self.signed()
        self.assertTrue(self.verifier.close("demo"))
        self.assertFalse(self.verifier.close("demo"))
        self.assertRejected("session", lambda: self.verifier.accept(envelope))
        self.verifier.issue_challenge("demo")
        self.assertRejected("challenge", lambda: self.verifier.accept(envelope))

    def test_duplicate_keys_at_any_level_are_rejected(self):
        raw = json.dumps(self.signed()).encode()
        for duplicate in (raw.replace(b'"version": 1', b'"version": 1, "version": 1'),
                          raw.replace(b'"ok": true', b'"ok": true, "ok": false')):
            self.assertRejected("schema", lambda duplicate=duplicate: loads_envelope(duplicate))

    def test_wire_rejects_malformed_and_nonfinite_values(self):
        for raw in (b"", b"{} trailing", b"\xff", b"[]", b"null", b'{"x":NaN}', b'{"x":Infinity}'):
            with self.subTest(raw=raw):
                self.assertRejected("schema", lambda raw=raw: loads_envelope(raw))
        raw = json.dumps(self.signed()).encode().replace(b'"ok": true', b'"ok": 1e400')
        self.assertRejected("schema", lambda: loads_envelope(raw))
        self.assertRejected("schema", lambda: loads_envelope("{}"))

    def test_size_event_and_tree_bounds(self):
        self.assertRejected("limit", lambda: loads_envelope(b" " * (MAX_ENVELOPE_BYTES + 1)))
        challenge = self.verifier.issue_challenge("demo")
        self.assertRejected("limit", lambda: sign_batch(SECRET, challenge, 0, EVENTS * (MAX_EVENTS + 1)))
        self.assertRejected("limit", lambda: sign_batch(SECRET, challenge, 0, []))
        for value in ("x" * 2049, [0] * 129, {str(i): i for i in range(65)}, 1 << 63):
            self.assertRejected("limit", lambda value=value: sign_batch(SECRET, challenge, 0, [{"kind": "sample", "payload": {"v": value}}]))
        deep = {}
        for _ in range(10):
            deep = {"nested": deep}
        self.assertRejected("limit", lambda: canonical_json(deep))
        cyclic = {}
        cyclic["x"] = cyclic
        self.assertRejected("limit", lambda: canonical_json(cyclic))
        many = [{"kind": "sample", "payload": {"value": "x" * 1000}} for _ in range(100)]
        self.assertRejected("limit", lambda: sign_batch(SECRET, challenge, 0, many))

    def test_strict_envelope_shape_and_types(self):
        valid = self.signed()
        for field, value, code in (("version", True, "version"), ("version", 2, "version"),
                                   ("sequence", True, "schema"), ("sequence", -1, "schema"),
                                   ("session_id", "a/b", "schema"), ("mac", "A" * 64, "schema"),
                                   ("challenge", "bad", "schema"), ("events", [{"kind": "Bad", "payload": {}}], "schema"),
                                   ("events", [{"kind": "valid", "payload": []}], "schema")):
            modified = dict(valid, **{field: value})
            with self.subTest(field=field, value=value):
                self.assertRejected(code, lambda modified=modified: self.verifier.accept(modified))
        self.assertRejected("schema", lambda: self.verifier.accept(dict(valid, surprise=1)))
        missing = dict(valid)
        del missing["mac"]
        self.assertRejected("schema", lambda: self.verifier.accept(missing))
        self.verifier.accept(valid)

    def test_signing_snapshots_mutable_inputs(self):
        events = copy.deepcopy(EVENTS)
        envelope = self.signed(events=events)
        events[0]["payload"]["path"] = "changed"
        self.verifier.accept(envelope)

    def test_unsupported_python_values_rejected(self):
        for value in (float("nan"), float("inf"), b"bytes", (1, 2), "\ud800"):
            self.assertRejected("schema", lambda value=value: canonical_json({"value": value}))

    def test_configuration_validation(self):
        for secret in (b"short", "a" * 32, bytearray(32)):
            with self.assertRaises(ValueError):
                SessionVerifier(secret)
        for kwargs in ({"challenge_ttl": 0}, {"challenge_ttl": float("nan")}, {"session_ttl": 1},
                       {"max_sessions": 0}, {"max_sessions": 257}, {"max_sessions": True}, {"clock": 1},
                       {"challenge_ttl": 1 << 1024}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                SessionVerifier(SECRET, **kwargs)

    def test_invalid_challenge_clock_values_are_controlled_errors(self):
        challenge = self.verifier.issue_challenge("demo")
        for value in (True, float("nan"), 1 << 1024):
            malformed = dict(challenge, expires_at=value)
            self.assertRejected("schema", lambda: sign_batch(SECRET, malformed, 0, EVENTS))
            verifier = SessionVerifier(SECRET, clock=lambda: value)
            with self.assertRaises(ValueError):
                verifier.issue_challenge("demo")


if __name__ == "__main__":
    unittest.main()
