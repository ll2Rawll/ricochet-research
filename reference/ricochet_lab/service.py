"""Loopback-only laboratory transport; not an Internet-facing service.

Possession of the lab key authenticates messages, not the computer or its claims.
The simulated rules consume claims from a caller, not authoritative game state.
"""

from __future__ import annotations

import json
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from typing import Any

from .protocol import MAX_ENVELOPE_BYTES, ProtocolError, SessionVerifier
from .rules import RuleEngine


class LabService:
    """Keep verifier and synthetic rule state together under one lock."""

    def __init__(self, secret: bytes, *, max_sessions: int = 64) -> None:
        self.verifier = SessionVerifier(secret, max_sessions=max_sessions)
        self._engines: dict[str, RuleEngine] = {}
        self._lock = threading.Lock()

    def _prune_engines(self) -> None:
        active = self.verifier.active_session_ids()
        self._engines = {key: value for key, value in self._engines.items() if key in active}

    def challenge(self, session_id: str) -> dict[str, Any]:
        with self._lock:
            challenge = self.verifier.issue_challenge(session_id)
            self._prune_engines()
            # A newly admitted verifier session must not inherit old game state.
            if challenge["next_sequence"] == 0:
                self._engines.pop(session_id, None)
            return challenge

    def accept(self, envelope: bytes | dict[str, Any]) -> dict[str, Any]:
        with self._lock:
            batch = self.verifier.accept(envelope)
            self._prune_engines()
            session_id = batch["session_id"]
            if session_id not in self._engines:
                self._engines[session_id] = RuleEngine(entitlements={"starter"})
            engine = self._engines[session_id]
            observations = sum(e["kind"] == "kernel_observation" for e in batch["events"])
            findings = engine.analyze(batch["events"])
            return {
                "version": 1,
                "session_id": session_id,
                "sequence": batch["sequence"],
                "event_count": len(batch["events"]),
                "kernel_observations": observations,
                "findings": findings,
                "assessment": "review" if findings else "no_rule_findings",
                "trust": "lab_key_possession_only",
            }


def _json_object(raw: bytes) -> dict[str, Any]:
    def pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate_key")
            result[key] = value
        return result

    def invalid_number(value: str) -> None:
        raise ValueError("non_finite_number")

    value = json.loads(raw.decode("utf-8"), object_pairs_hook=pairs,
                       parse_constant=invalid_number)
    if not isinstance(value, dict):
        raise ValueError("object_required")
    return value


class LabHTTPServer(HTTPServer):
    """A bounded, single-request-at-a-time local demonstration server."""

    allow_reuse_address = False

    def __init__(self, secret: bytes, port: int = 0) -> None:
        self.service = LabService(secret)
        super().__init__(("127.0.0.1", port), LabHandler)

    def get_request(self):
        sock, address = super().get_request()
        sock.settimeout(3.0)
        return sock, address


class LabHandler(BaseHTTPRequestHandler):
    server: LabHTTPServer
    server_version = "ReferenceLab/0.1"
    sys_version = ""

    def log_message(self, format: str, *args: Any) -> None:
        # Do not write session identifiers, telemetry or secrets to access logs.
        pass

    def _reply(self, status: int, body: dict[str, Any]) -> None:
        encoded = json.dumps(body, separators=(",", ":"), allow_nan=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(encoded)
        self.close_connection = True

    def _local_request(self) -> bool:
        hosts = self.headers.get_all("Host", [])
        port = self.server.server_address[1]
        allowed = {f"127.0.0.1:{port}", f"localhost:{port}"}
        # Reject browser cross-origin requests and arbitrary DNS names resolving
        # to loopback. There is deliberately no CORS support.
        if len(hosts) != 1 or hosts[0] not in allowed or "Origin" in self.headers:
            self._reply(403, {"error": "local_client_required"})
            return False
        return True

    def do_GET(self) -> None:
        if not self._local_request():
            return
        if self.path == "/health":
            self._reply(200, {"status": "lab_only", "version": 1})
        else:
            self._reply(404, {"error": "not_found"})

    def do_POST(self) -> None:
        if not self._local_request():
            return
        if self.path not in {"/v1/challenge", "/v1/batch"}:
            self._reply(404, {"error": "not_found"})
            return
        lengths = self.headers.get_all("Content-Length", [])
        content_types = self.headers.get_all("Content-Type", [])
        if ("Transfer-Encoding" in self.headers or len(lengths) != 1
                or not lengths[0].isascii() or not lengths[0].isdigit()
                or len(lengths[0]) > 8):
            self._reply(400, {"error": "invalid_length"})
            return
        length = int(lengths[0])
        if not 1 <= length <= MAX_ENVELOPE_BYTES:
            self._reply(413, {"error": "body_too_large_or_empty"})
            return
        if (len(content_types) != 1
                or content_types[0].split(";", 1)[0].strip() != "application/json"):
            self._reply(415, {"error": "json_required"})
            return
        try:
            raw = self.rfile.read(length)
            if len(raw) != length:
                self._reply(400, {"error": "incomplete_body"})
                return
            if self.path == "/v1/challenge":
                body = _json_object(raw)
                if set(body) != {"session_id"}:
                    raise ValueError("invalid_challenge_shape")
                response = self.server.service.challenge(body["session_id"])
            else:
                response = self.server.service.accept(raw)
            self._reply(200, response)
        except ProtocolError as exc:
            self._reply(400, {"error": exc.code})
        except (ValueError, TypeError, RecursionError, UnicodeError):
            self._reply(400, {"error": "invalid_request"})
        except (TimeoutError, OSError):
            self.close_connection = True
