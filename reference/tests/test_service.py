import http.client
import json
import threading
import unittest

from ricochet_lab.protocol import SessionVerifier, sign_batch
from ricochet_lab.service import LabHTTPServer, LabService

KEY = b"local test key, not a credential!!"


class ServiceTests(unittest.TestCase):
    def setUp(self):
        self.server = LabHTTPServer(KEY)
        self.thread = threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.01})
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.thread.join(timeout=2)
        self.server.server_close()

    def request(self, path, body, headers=None, *, raw=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=3)
        try:
            all_headers = {"Content-Type": "application/json", **(headers or {})}
            connection.request("POST", path, body=raw if raw is not None else json.dumps(body).encode(), headers=all_headers)
            response = connection.getresponse()
            return response.status, json.load(response)
        finally:
            connection.close()

    def batch(self, session, events):
        status, challenge = self.request("/v1/challenge", {"session_id": session})
        self.assertEqual(status, 200)
        return sign_batch(KEY, challenge, challenge["next_sequence"], events)

    def test_http_state_survives_batches_and_replay_is_rejected(self):
        first = self.batch("player", [{"kind": "shot", "payload": {"tick": 1, "time": 0}}])
        status, result = self.request("/v1/batch", first)
        self.assertEqual((status, result["findings"]), (200, []))
        second = self.batch("player", [{"kind": "shot", "payload": {"tick": 2, "time": 0.01}}])
        status, result = self.request("/v1/batch", second)
        self.assertEqual(status, 200)
        self.assertEqual(result["findings"][0]["rule_id"], "LAB_SHOT_RATE")
        self.assertEqual(self.request("/v1/batch", second)[0], 400)

    def test_tamper_does_not_consume_valid_challenge(self):
        signed = self.batch("tamper", [{"kind": "equip", "payload": {"item": "starter"}}])
        tampered = dict(signed, mac="0" * 64)
        status, error = self.request("/v1/batch", tampered)
        self.assertEqual((status, error["error"]), (400, "mac"))
        self.assertEqual(self.request("/v1/batch", signed)[0], 200)

    def test_local_browser_and_rebinding_boundaries(self):
        for headers in ({"Origin": "https://example.test"}, {"Host": "example.test"}):
            self.assertEqual(self.request("/v1/challenge", {"session_id": "x"}, headers)[0], 403)

    def test_malformed_and_ambiguous_requests(self):
        self.assertEqual(self.request("/v1/challenge", {}, raw=b'{"session_id":"a","session_id":"b"}')[0], 400)
        self.assertEqual(self.request("/v1/challenge", {"session_id": []})[0], 400)
        self.assertEqual(self.request("/v1/challenge", {"session_id": "x"}, {"Content-Type": "text/plain"})[0], 415)
        self.assertEqual(self.request("/v1/challenge", {}, {"Content-Length": "65537"})[0], 413)
        self.assertEqual(self.request("/v1/challenge", {}, {"Transfer-Encoding": "chunked"})[0], 400)
        self.assertEqual(self.request("/missing", {})[0], 404)

    def test_duplicate_length_header_rejected(self):
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=3)
        try:
            connection.putrequest("POST", "/v1/challenge")
            connection.putheader("Content-Type", "application/json")
            connection.putheader("Content-Length", "2")
            connection.putheader("Content-Length", "2")
            connection.endheaders(b"{}")
            response = connection.getresponse()
            self.assertEqual(response.status, 400)
            response.read()
        finally:
            connection.close()

    def test_observation_is_never_gameplay_verdict(self):
        signed = self.batch("kernel", [{"kind": "kernel_observation", "payload": {"kind": 6}}])
        status, result = self.request("/v1/batch", signed)
        self.assertEqual((status, result["findings"], result["kernel_observations"]), (200, [], 1))
        self.assertEqual(result["trust"], "lab_key_possession_only")

    def test_expired_session_discards_only_corresponding_game_state(self):
        now = [0.0]
        service = LabService(KEY, max_sessions=2)
        service.verifier = SessionVerifier(KEY, clock=lambda: now[0], challenge_ttl=1, session_ttl=10, max_sessions=2)
        def shoot(name, tick, time):
            challenge = service.challenge(name)
            return service.accept(sign_batch(KEY, challenge, challenge["next_sequence"],
                                            [{"kind": "shot", "payload": {"tick": tick, "time": time}}]))
        shoot("expired", 1, 0)
        now[0] = 5
        shoot("active", 1, 0)
        now[0] = 10
        service.challenge("new")
        self.assertEqual(shoot("active", 2, 0.01)["findings"][0]["rule_id"], "LAB_SHOT_RATE")
        now[0] = 21
        self.assertEqual(shoot("expired", 1, 0)["findings"], [])


if __name__ == "__main__":
    unittest.main()
