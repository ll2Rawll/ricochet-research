"""Bounded HMAC-chained diagnostic journals for the local sandbox.

This is an original lab format, not a RICOCHET replay format. A valid chain
authenticates records to holders of a shared secret; it does not establish
honest telemetry or protect against a compromised producer or stolen key.
A separately trusted final checkpoint is required to detect tail truncation.
"""

from __future__ import annotations

import hashlib
import hmac
import json
import math
import os
import re
from pathlib import Path
from collections.abc import Iterable

MAX_RECORD_BYTES = 65536
MAX_RECORDS = 10000
MAX_TOTAL_BYTES = 16 * 1024 * 1024
MAX_DEPTH = 16
MAX_NODES = 4096
_GENESIS = "0" * 64
_DOMAIN = b"ricochet-reference/journal/v1\x00"
_HEX_MAC = re.compile(r"[0-9a-f]{64}\Z")


class JournalError(ValueError):
    """A journal, key, or checkpoint fails the lab format's validation."""


def _key(key: bytes) -> bytes:
    if not isinstance(key, bytes) or len(key) < 32:
        raise JournalError("journal key must contain at least 32 bytes")
    return key


def _validate_json(value: object) -> None:
    nodes = 0

    def visit(item: object, depth: int) -> None:
        nonlocal nodes
        nodes += 1
        if nodes > MAX_NODES or depth > MAX_DEPTH:
            raise JournalError("record JSON exceeds structural limits")
        if item is None or type(item) in (str, bool):
            return
        if type(item) is int:
            if not -(2**63) <= item < 2**63:
                raise JournalError("integer exceeds signed 64-bit range")
            return
        if type(item) is float:
            if not math.isfinite(item):
                raise JournalError("non-finite numbers are not supported")
            return
        if type(item) is list:
            for child in item:
                visit(child, depth + 1)
            return
        if type(item) is dict:
            for name, child in item.items():
                if type(name) is not str:
                    raise JournalError("JSON object keys must be strings")
                visit(name, depth + 1)
                visit(child, depth + 1)
            return
        raise JournalError("record contains an unsupported JSON value")

    visit(value, 0)


def _canonical(value: dict) -> bytes:
    _validate_json(value)
    try:
        return json.dumps(
            value, sort_keys=True, separators=(",", ":"),
            ensure_ascii=False, allow_nan=False,
        ).encode("utf-8")
    except (TypeError, ValueError, UnicodeError, RecursionError) as exc:
        raise JournalError("record is not canonicalizable UTF-8 JSON") from exc


def _pairs(pairs: list) -> dict:
    result = {}
    for name, value in pairs:
        if name in result:
            raise JournalError("duplicate JSON key")
        result[name] = value
    return result


def _constant(_: str) -> None:
    raise JournalError("non-finite JSON constant")


def _decode(raw: bytes) -> dict:
    try:
        value = json.loads(
            raw.decode("utf-8"), object_pairs_hook=_pairs,
            parse_constant=_constant,
        )
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise JournalError("invalid or ambiguous journal JSON") from exc
    if type(value) is not dict:
        raise JournalError("journal line must be an object")
    _validate_json(value)
    return value


def _checkpoint(count: int, final_mac: str) -> dict:
    return {"version": 1, "count": count, "final_mac": final_mac}


def _validate_checkpoint(checkpoint: dict) -> None:
    if type(checkpoint) is not dict or set(checkpoint) != {
        "version", "count", "final_mac"
    }:
        raise JournalError("invalid checkpoint shape")
    if type(checkpoint["version"]) is not int or checkpoint["version"] != 1:
        raise JournalError("unsupported checkpoint version")
    count = checkpoint["count"]
    mac = checkpoint["final_mac"]
    if type(count) is not int or not 0 <= count <= MAX_RECORDS:
        raise JournalError("invalid checkpoint count")
    if not isinstance(mac, str) or _HEX_MAC.fullmatch(mac) is None:
        raise JournalError("invalid checkpoint MAC")


def write_journal(path: str | Path, records: Iterable[dict], key: bytes) -> dict:
    """Create a new journal and return its final checkpoint after flushing.

    Existing paths, including symlinks, are refused. New files use mode 0600
    on POSIX; Windows access inherits the containing directory's ACL. A write
    failure can leave an incomplete file and returns no valid checkpoint.
    Retain the returned checkpoint in a separately trusted location. The
    writer does not open existing journals for append or recovery.
    """
    key = _key(key)
    try:
        iterator = iter(records)
    except TypeError as exc:
        raise JournalError("records must be iterable") from exc
    previous, count, total_bytes = _GENESIS, 0, 0
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    flags |= getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags, 0o600)
    with os.fdopen(descriptor, "wb") as stream:
        for record in iterator:
            if count >= MAX_RECORDS:
                raise JournalError("too many journal records")
            if type(record) is not dict:
                raise JournalError("record must be an object")
            body = {
                "version": 1, "index": count,
                "previous_mac": previous, "record": record,
            }
            body_bytes = _canonical(body)
            mac = hmac.new(key, _DOMAIN + body_bytes, hashlib.sha256).hexdigest()
            encoded = _canonical({**body, "mac": mac}) + b"\n"
            total_bytes += len(encoded)
            if len(encoded) > MAX_RECORD_BYTES or total_bytes > MAX_TOTAL_BYTES:
                raise JournalError("journal exceeds byte limits")
            stream.write(encoded)
            count += 1
            previous = mac
        stream.flush()
        os.fsync(stream.fileno())
    return _checkpoint(count, previous)


def read_journal(
    path: str | Path, key: bytes, checkpoint: dict | None = None
) -> list[dict]:
    """Verify each record before returning the bounded journal's records.

    With no checkpoint, a valid prefix is accepted: truncating at a newline
    cannot then be distinguished from an intentionally shorter journal.
    Records are returned only after the complete file passes verification.
    """
    key = _key(key)
    if checkpoint is not None:
        _validate_checkpoint(checkpoint)
    previous, total_bytes = _GENESIS, 0
    records = []
    with open(path, "rb") as stream:
        while True:
            raw = stream.readline(MAX_RECORD_BYTES + 1)
            if not raw:
                break
            total_bytes += len(raw)
            if len(raw) > MAX_RECORD_BYTES or total_bytes > MAX_TOTAL_BYTES:
                raise JournalError("journal exceeds byte limits")
            if not raw.endswith(b"\n"):
                raise JournalError("unterminated journal record")
            if len(records) >= MAX_RECORDS:
                raise JournalError("too many journal records")
            line = _decode(raw)
            if set(line) != {"version", "index", "previous_mac", "record", "mac"}:
                raise JournalError("invalid journal envelope shape")
            if type(line["version"]) is not int or line["version"] != 1:
                raise JournalError("unsupported journal version")
            if type(line["index"]) is not int or line["index"] != len(records):
                raise JournalError("nonconsecutive journal index")
            if type(line["record"]) is not dict:
                raise JournalError("record must be an object")
            if line["previous_mac"] != previous:
                raise JournalError("journal chain link mismatch")
            mac = line["mac"]
            if not isinstance(mac, str) or _HEX_MAC.fullmatch(mac) is None:
                raise JournalError("invalid journal MAC")
            body = {name: value for name, value in line.items() if name != "mac"}
            expected = hmac.new(
                key, _DOMAIN + _canonical(body), hashlib.sha256
            ).hexdigest()
            if not hmac.compare_digest(mac, expected):
                raise JournalError("journal authentication failed")
            records.append(line["record"])
            previous = mac
    if checkpoint is not None:
        if checkpoint["count"] != len(records) or not hmac.compare_digest(
            checkpoint["final_mac"], previous
        ):
            raise JournalError("trusted final checkpoint mismatch")
    return records
