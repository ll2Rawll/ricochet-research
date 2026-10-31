"""An experimental, local shared-key evidence protocol.

This is independently authored reference code, not RICOCHET's protocol. A valid
HMAC proves possession of a laboratory secret; it does not establish a genuine
driver, honest measurements, a trusted device, TPM attestation, or cheat status.
The server keeps all state in memory. Restarting it discards sessions.

The default clock is monotonic. Challenge ``expires_at`` values are meaningful
only in this server's clock domain. Clients should not interpret them as UTC.
"""

from __future__ import annotations

import hashlib
import hmac
import json
import math
import re
import secrets
import threading
import time
from dataclasses import dataclass
from typing import Any, Callable

VERSION = 1
MAX_ENVELOPE_BYTES = 65_536
MAX_EVENTS = 128
MAX_STRING_LENGTH = 2_048
MAX_OBJECT_KEYS = 64
MAX_ARRAY_ITEMS = 128
MAX_NESTING = 8
MAX_JSON_NODES = 4_096
MAX_SEQUENCE = (1 << 63) - 1
MAX_SESSIONS = 256
MIN_SECRET_BYTES = 32

_SESSION_RE = re.compile(r"[A-Za-z0-9_-]{1,64}\Z")
_KIND_RE = re.compile(r"[a-z][a-z0-9_.-]{0,63}\Z")
_HEX_RE = re.compile(r"[0-9a-f]{64}\Z")
_ENVELOPE_KEYS = {"version", "session_id", "challenge", "sequence", "events", "mac"}
_CHALLENGE_KEYS = {"version", "session_id", "nonce", "expires_at", "next_sequence"}
_DOMAIN = b"ricochet-lab/evidence/v1\x00"


class ProtocolError(ValueError):
    """A rejected laboratory message; ``code`` is suitable for local reports."""

    def __init__(self, code: str, message: str) -> None:
        self.code = code
        super().__init__(message)


def _fail(code: str, message: str) -> None:
    raise ProtocolError(code, message)


def _secret(secret: bytes) -> bytes:
    if type(secret) is not bytes or len(secret) < MIN_SECRET_BYTES:
        raise ValueError(f"secret must be bytes with at least {MIN_SECRET_BYTES} bytes")
    return secret


def _finite_number(value: Any) -> bool:
    if type(value) not in (int, float):
        return False
    try:
        return math.isfinite(value)
    except OverflowError:
        return False


def _session_id(value: Any) -> str:
    if type(value) is not str or _SESSION_RE.fullmatch(value) is None:
        _fail("schema", "session_id must contain 1..64 ASCII letters, digits, '_' or '-'")
    return value


def _sequence(value: Any) -> int:
    if type(value) is not int or not 0 <= value <= MAX_SEQUENCE:
        _fail("schema", "sequence must be an integer in [0, 2**63-1]")
    return value


def _json_tree(value: Any, *, depth: int = 0, count: list[int] | None = None) -> None:
    """Bound object work before copying/canonicalizing even direct Python inputs."""
    if count is None:
        count = [0]
    count[0] += 1
    if count[0] > MAX_JSON_NODES or depth > MAX_NESTING:
        _fail("limit", "JSON node or nesting limit exceeded")
    if value is None or type(value) is bool:
        return
    if type(value) is str:
        if len(value) > MAX_STRING_LENGTH:
            _fail("limit", "JSON string limit exceeded")
        try:
            value.encode("utf-8")
        except UnicodeEncodeError:
            _fail("schema", "JSON strings must contain valid Unicode scalar values")
        return
    if type(value) is int:
        if not -MAX_SEQUENCE <= value <= MAX_SEQUENCE:
            _fail("limit", "JSON integers must fit the supported signed 63-bit magnitude")
        return
    if type(value) is float:
        if not math.isfinite(value):
            _fail("schema", "non-finite numbers are not permitted")
        return
    if type(value) is list:
        if len(value) > MAX_ARRAY_ITEMS:
            _fail("limit", "JSON array item limit exceeded")
        for item in value:
            _json_tree(item, depth=depth + 1, count=count)
        return
    if type(value) is dict:
        if len(value) > MAX_OBJECT_KEYS:
            _fail("limit", "JSON object key limit exceeded")
        for key, item in value.items():
            if type(key) is not str or len(key) > 128:
                _fail("schema", "JSON object keys must be strings of at most 128 characters")
            _json_tree(key, depth=depth + 1, count=count)
            _json_tree(item, depth=depth + 1, count=count)
        return
    _fail("schema", "only plain JSON value types are permitted")


def canonical_json(value: Any) -> bytes:
    """Canonical form for this Python lab protocol, not a general JSON standard.

    Sorting keys and compact UTF-8 encoding are deterministic for supported
    Python values. Cross-language deployments need a specified canonicalization
    standard and interoperability tests, especially for floating-point values.
    """
    _json_tree(value)
    try:
        encoded = json.dumps(
            value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False
        ).encode("utf-8")
    except (ValueError, TypeError, UnicodeEncodeError, RecursionError) as exc:
        raise ProtocolError("schema", "cannot encode canonical JSON") from exc
    if len(encoded) > MAX_ENVELOPE_BYTES:
        _fail("limit", "canonical JSON exceeds the message byte limit")
    return encoded


def _validate_body(body: Any) -> None:
    if type(body) is not dict or set(body) != _ENVELOPE_KEYS - {"mac"}:
        _fail("schema", "unexpected evidence body fields")
    if type(body["version"]) is not int or body["version"] != VERSION:
        _fail("version", "unsupported protocol version")
    _session_id(body["session_id"])
    _sequence(body["sequence"])
    if type(body["challenge"]) is not str or _HEX_RE.fullmatch(body["challenge"]) is None:
        _fail("schema", "challenge must be a 32-byte lowercase hexadecimal nonce")
    events = body["events"]
    if type(events) is not list or not 1 <= len(events) <= MAX_EVENTS:
        _fail("limit", f"events must contain 1..{MAX_EVENTS} entries")
    for event in events:
        if type(event) is not dict or set(event) != {"kind", "payload"}:
            _fail("schema", "events require exactly kind and payload")
        if type(event["kind"]) is not str or _KIND_RE.fullmatch(event["kind"]) is None:
            _fail("schema", "event kind must be a lowercase ASCII identifier")
        if type(event["payload"]) is not dict:
            _fail("schema", "event payload must be a JSON object")


def _validated_envelope(envelope: Any) -> dict[str, Any]:
    if type(envelope) is not dict or set(envelope) != _ENVELOPE_KEYS:
        _fail("schema", "unexpected envelope fields")
    body = {key: value for key, value in envelope.items() if key != "mac"}
    _validate_body(body)
    if type(envelope["mac"]) is not str or _HEX_RE.fullmatch(envelope["mac"]) is None:
        _fail("schema", "mac must be a SHA-256 lowercase hexadecimal digest")
    # The returned snapshot is detached from the caller's mutable containers.
    return json.loads(canonical_json(envelope))


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            _fail("schema", "duplicate JSON keys are not permitted")
        result[key] = value
    return result


def _invalid_constant(value: str) -> Any:
    _fail("schema", f"non-finite JSON constant {value} is not permitted")


def loads_envelope(raw: bytes) -> dict[str, Any]:
    """Decode a bounded UTF-8 JSON envelope, rejecting duplicate object keys."""
    if type(raw) is not bytes:
        _fail("schema", "wire input must be bytes")
    if len(raw) > MAX_ENVELOPE_BYTES:
        _fail("limit", "wire message exceeds the byte limit")
    try:
        decoded = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs, parse_constant=_invalid_constant)
    except ProtocolError:
        raise
    except (UnicodeDecodeError, json.JSONDecodeError, RecursionError, ValueError) as exc:
        raise ProtocolError("schema", "invalid UTF-8 JSON envelope") from exc
    return _validated_envelope(decoded)


def sign_batch(
    secret: bytes, challenge: dict[str, Any], sequence: int, events: list[dict[str, Any]]
) -> dict[str, Any]:
    """Sign simulated/local evidence; this does not make its contents true."""
    _secret(secret)
    if type(challenge) is not dict or set(challenge) != _CHALLENGE_KEYS:
        _fail("schema", "unexpected challenge response fields")
    if not _finite_number(challenge["expires_at"]):
        _fail("schema", "challenge expiry must be finite")
    _sequence(challenge["next_sequence"])
    if sequence != challenge["next_sequence"]:
        _fail("sequence", "sequence does not match the challenge response")
    body = {
        "version": challenge["version"],
        "session_id": challenge["session_id"],
        "challenge": challenge["nonce"],
        "sequence": sequence,
        "events": events,
    }
    _validate_body(body)
    encoded = canonical_json(body)
    # Decode the encoded snapshot so subsequent caller mutations do not change
    # the object whose bytes were authenticated.
    signed = json.loads(encoded)
    signed["mac"] = hmac.new(secret, _DOMAIN + encoded, hashlib.sha256).hexdigest()
    return _validated_envelope(signed)


@dataclass
class _Session:
    next_sequence: int
    last_activity: float
    nonce: str | None = None
    challenge_expires: float = 0.0


class SessionVerifier:
    """Bounded in-memory session state and atomic one-use evidence acceptance.

    All collectors in one instance share a lab key. There is no enrollment,
    account authentication, remote attestation, durable replay store, or TLS.
    Retrying an accepted envelope is rejected, so transports must distinguish
    acknowledgement loss from rejection. Session closure invalidates its nonce.
    """

    def __init__(
        self,
        secret: bytes,
        *,
        clock: Callable[[], float] = time.monotonic,
        challenge_ttl: float = 30.0,
        session_ttl: float = 600.0,
        max_sessions: int = MAX_SESSIONS,
    ) -> None:
        self._secret = _secret(secret)
        if not callable(clock):
            raise ValueError("clock must be callable")
        for name, value in (("challenge_ttl", challenge_ttl), ("session_ttl", session_ttl)):
            if not _finite_number(value) or value <= 0:
                raise ValueError(f"{name} must be a positive finite number")
        if session_ttl < challenge_ttl:
            raise ValueError("session_ttl must be at least challenge_ttl")
        if type(max_sessions) is not int or not 1 <= max_sessions <= MAX_SESSIONS:
            raise ValueError(f"max_sessions must be between 1 and {MAX_SESSIONS}")
        self._clock = clock
        self._challenge_ttl = float(challenge_ttl)
        self._session_ttl = float(session_ttl)
        self._max_sessions = max_sessions
        self._sessions: dict[str, _Session] = {}
        self._lock = threading.Lock()

    def _now(self) -> float:
        now = self._clock()
        if not _finite_number(now):
            raise ValueError("clock must return a finite number")
        return float(now)

    def _prune(self, now: float) -> None:
        expired = [name for name, state in self._sessions.items() if now - state.last_activity >= self._session_ttl]
        for name in expired:
            del self._sessions[name]

    @property
    def session_count(self) -> int:
        with self._lock:
            self._prune(self._now())
            return len(self._sessions)

    def active_session_ids(self) -> frozenset[str]:
        """Prune expired state and return a detached set for application cleanup."""
        with self._lock:
            self._prune(self._now())
            return frozenset(self._sessions)

    def issue_challenge(self, session_id: str) -> dict[str, Any]:
        """Return the outstanding challenge or issue a fresh 256-bit nonce.

        Repeated requests do not invalidate or extend a live challenge. After
        acceptance/expiry, a new challenge carries the next required sequence.
        """
        _session_id(session_id)
        with self._lock:
            now = self._now()
            self._prune(now)
            state = self._sessions.get(session_id)
            if state is None:
                if len(self._sessions) >= self._max_sessions:
                    _fail("capacity", "session capacity reached")
                state = _Session(next_sequence=0, last_activity=now)
                self._sessions[session_id] = state
            if state.next_sequence > MAX_SEQUENCE:
                _fail("sequence", "session sequence is exhausted; open a new session")
            if state.nonce is None or now >= state.challenge_expires:
                state.nonce = secrets.token_hex(32)
                state.challenge_expires = now + self._challenge_ttl
            state.last_activity = now
            return {
                "version": VERSION,
                "session_id": session_id,
                "nonce": state.nonce,
                "expires_at": state.challenge_expires,
                "next_sequence": state.next_sequence,
            }

    def accept(self, envelope: dict[str, Any] | bytes) -> dict[str, Any]:
        """Authenticate and atomically consume an outstanding challenge.

        Returns a detached body without its MAC. Specific event semantics must
        be checked by the application; successful protocol validation says
        nothing about the truth or meaning of the supplied measurements.
        """
        message = loads_envelope(envelope) if type(envelope) is bytes else _validated_envelope(envelope)
        received_mac = message.pop("mac")
        expected_mac = hmac.new(self._secret, _DOMAIN + canonical_json(message), hashlib.sha256).hexdigest()
        if not hmac.compare_digest(received_mac, expected_mac):
            _fail("mac", "message authentication failed")
        with self._lock:
            now = self._now()
            self._prune(now)
            state = self._sessions.get(message["session_id"])
            if state is None:
                _fail("session", "unknown or expired session")
            if state.nonce is None:
                _fail("challenge", "no outstanding challenge; a prior challenge may have been consumed")
            if now >= state.challenge_expires:
                _fail("expired", "challenge expired")
            if not hmac.compare_digest(message["challenge"], state.nonce):
                _fail("challenge", "challenge does not match this session")
            if message["sequence"] != state.next_sequence:
                _fail("sequence", "unexpected batch sequence")
            state.nonce = None
            state.challenge_expires = 0.0
            state.next_sequence += 1
            state.last_activity = now
            return message

    def close(self, session_id: str) -> bool:
        """Discard a session. Returns whether one existed, without creating it."""
        _session_id(session_id)
        with self._lock:
            return self._sessions.pop(session_id, None) is not None
