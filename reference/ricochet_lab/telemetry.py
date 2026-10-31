"""Strict importer for our collector's JSONL, not for a Call of Duty log.

Framing validation catches malformed/truncated exports. It cannot authenticate
their producer. Kernel event loss is retained explicitly in batch observations.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

SCHEMA = "ricochet-reference-kernel/1"
MAX_RECORDS = 4096
MAX_LINE_BYTES = 4096
MAX_FILE_BYTES = 2 * 1024 * 1024
_FIELDS = {
    "session": {"session_id", "target_process_id"},
    "batch": {"session_id", "event_count", "dropped_events", "next_sequence"},
    "event": {"session_id", "sequence", "time_100ns", "process_id", "thread_id",
              "related_process_id", "image_size", "kind", "flags"},
    "stop": {"session_id"},
}


def _pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError("duplicate kernel log key")
        result[key] = value
    return result


def read_kernel_log(path: str | Path) -> dict[str, Any]:
    """Read one completed collector session and preserve every control record.

    Numbers are limited to 2**63-1 for the Python lab protocol. The native ABI
    uses uint64, so exports outside that range are intentionally unsupported.
    """
    observations = []
    session_id = None
    expected = 0
    sequence = 0
    next_sequence = 0
    dropped = 0
    events = 0
    stopped = False
    total = 0
    with Path(path).open("rb") as stream:
        for index in range(MAX_RECORDS + 1):
            line = stream.readline(MAX_LINE_BYTES + 1)
            if not line:
                break
            total += len(line)
            if index == MAX_RECORDS or len(line) > MAX_LINE_BYTES or total > MAX_FILE_BYTES:
                raise ValueError("kernel log exceeds import budget")
            if not line.endswith(b"\n") or stopped:
                raise ValueError("incomplete or trailing kernel log record")
            try:
                record = json.loads(line.decode("utf-8"), object_pairs_hook=_pairs)
            except (ValueError, UnicodeError, RecursionError) as exc:
                raise ValueError("invalid kernel log JSON") from exc
            if type(record) is not dict or record.get("schema") != SCHEMA:
                raise ValueError("unsupported kernel log schema")
            kind = record.get("type")
            if type(kind) is not str or kind not in _FIELDS:
                raise ValueError("unsupported kernel log record")
            if set(record) != _FIELDS[kind] | {"schema", "type"}:
                raise ValueError("unexpected kernel log fields")
            for key in _FIELDS[kind]:
                if type(record[key]) is not int or not 0 <= record[key] <= 2**63 - 1:
                    raise ValueError("invalid kernel log integer")
            if kind == "session":
                if session_id is not None or index != 0 or record["session_id"] == 0 or record["target_process_id"] == 0:
                    raise ValueError("invalid session start")
                session_id = record["session_id"]
            elif session_id is None or record["session_id"] != session_id:
                raise ValueError("kernel session mismatch")
            elif kind == "batch":
                if expected or record["event_count"] > 32:
                    raise ValueError("invalid batch framing")
                if record["next_sequence"] < next_sequence or record["next_sequence"] <= sequence:
                    raise ValueError("kernel sequence regression")
                if record["dropped_events"] < dropped:
                    raise ValueError("kernel loss counter regression")
                expected = record["event_count"]
                next_sequence = record["next_sequence"]
                dropped = record["dropped_events"]
            elif kind == "event":
                if not expected or not sequence < record["sequence"] < next_sequence:
                    raise ValueError("event is outside its batch")
                if not 1 <= record["kind"] <= 6 or record["flags"] != 0:
                    raise ValueError("unknown kernel event kind or flags")
                expected -= 1
                sequence = record["sequence"]
                events += 1
            elif kind == "stop":
                if expected:
                    raise ValueError("truncated batch")
                stopped = True
            observations.append({"kind": "kernel_observation", "payload": record})
    if not stopped:
        raise ValueError("kernel log has no completed stop record")
    return {
        "events": observations,
        "summary": {"session_id": session_id, "native_events": events,
                    "dropped_events": dropped, "completed": True,
                    "authenticity": "not_established"},
    }
