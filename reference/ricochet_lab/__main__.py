"""Command-line entry point for the independent reference lab."""

from __future__ import annotations

import argparse
import copy
import http.client
import json
import math
import os
from pathlib import Path
import secrets
import sys
import tempfile

from .integrity import build_manifest, verify_manifest
from .protocol import ProtocolError, sign_batch
from .replay import read_journal, write_journal
from .service import LabHTTPServer, LabService
from .telemetry import read_kernel_log
from .visibility import visible_entities


def emit(value) -> None:
    print(json.dumps(value, indent=2, allow_nan=False))


def read_key(path: str) -> bytes:
    with Path(path).open("rb") as stream:
        key = stream.read(65)
    if len(key) != 32:
        raise ValueError("key file must contain exactly 32 raw bytes; use keygen")
    return key


def decode_json(raw: bytes):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate JSON key")
            result[key] = value
        return result

    def invalid(value):
        raise ValueError("non-finite JSON number")

    def finite_float(value):
        number = float(value)
        if not math.isfinite(number):
            raise ValueError("non-finite JSON number")
        return number

    return json.loads(raw.decode("utf-8"), object_pairs_hook=pairs,
                      parse_constant=invalid, parse_float=finite_float)


def read_json(path: str):
    with Path(path).open("rb") as stream:
        raw = stream.read(2 * 1024 * 1024 + 1)
    if len(raw) > 2 * 1024 * 1024:
        raise ValueError("input JSON exceeds 2 MiB")
    return decode_json(raw)


def post(port: int, route: str, body: dict) -> dict:
    raw = json.dumps(body, allow_nan=False, separators=(",", ":")).encode()
    # A fixed loopback HTTPConnection cannot follow redirects or send local
    # evidence to an inherited Internet proxy.
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    try:
        connection.request("POST", route, body=raw, headers={"Content-Type": "application/json"})
        response = connection.getresponse()
        if response.status != 200:
            raise ValueError(f"local server rejected request with HTTP {response.status}")
        response_raw = response.read(65537)
        if len(response_raw) > 65536:
            raise ValueError("local server response exceeds 64 KiB")
        value = decode_json(response_raw)
        if type(value) is not dict:
            raise ValueError("local server response must be a JSON object")
        return value
    finally:
        connection.close()


def demo() -> dict:
    """Run invented events and owned temporary files without installing a driver."""
    key = secrets.token_bytes(32)
    service = LabService(key)
    challenge = service.challenge("synthetic-demo")
    events = [
        {"kind": "movement", "payload": {"tick": 1, "time": 0, "position": [0, 0, 0]}},
        {"kind": "movement", "payload": {"tick": 2, "time": 1, "position": [4, 0, 0]}},
        {"kind": "equip", "payload": {"item": "starter"}},
        {"kind": "movement", "payload": {"tick": 3, "time": 2, "position": [100, 0, 0]}},
        {"kind": "equip", "payload": {"item": "unowned-demo-item"}},
        {"kind": "shot", "payload": {"tick": 1, "time": 0}},
        {"kind": "shot", "payload": {"tick": 2, "time": 0.01}},
    ]
    envelope = sign_batch(key, challenge, 0, events)
    tampered = copy.deepcopy(envelope)
    tampered["events"][0]["payload"]["position"][0] = 99
    try:
        service.accept(tampered)
        raise RuntimeError("demo expected tampering to be rejected")
    except ProtocolError as exc:
        tamper_rejection = exc.code
    result = service.accept(envelope)
    try:
        service.accept(envelope)
        raise RuntimeError("demo expected replay to be rejected")
    except ProtocolError as exc:
        replay_rejection = exc.code
    entities = [
        {"id": "observer", "position": [0, 0, 0]},
        {"id": "near", "position": [0, 3, 0]},
        {"id": "behind-wall", "position": [5, 0, 0]},
        {"id": "far", "position": [0, 30, 0]},
    ]
    visible = visible_entities("observer", entities, max_distance=10,
                               occluders=[{"min": [2, -1, -1], "max": [3, 1, 1]}])
    with tempfile.TemporaryDirectory(prefix="ricochet-reference-") as directory:
        root = Path(directory)
        owned = root / "owned-demo-asset.txt"
        owned.write_text("baseline\n", encoding="utf-8")
        manifest = build_manifest(root, [owned.name])
        before = verify_manifest(root, manifest)
        owned.write_text("modified\n", encoding="utf-8")
        after = verify_manifest(root, manifest)
        journal = root / "review.jsonl"
        checkpoint = write_journal(journal, [{"events": events, "review": result}], key)
        replayed = read_journal(journal, key, checkpoint)
    return {
        "scope": "independent_synthetic_lab_no_game_integration",
        "trust": "HMAC proves lab key possession, not device integrity",
        "review": result,
        "tampered_message_rejected": tamper_rejection,
        "replayed_message_rejected": replay_rejection,
        "integrity_before": before,
        "integrity_after": after,
        "visible_synthetic_entities": [entity["id"] for entity in visible],
        "journal_records_verified_with_checkpoint": len(replayed),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("demo", help="run all portable layers on invented data")
    keygen = sub.add_parser("keygen", help="create a new 32-byte local lab key")
    keygen.add_argument("path")
    serve = sub.add_parser("serve", help="run loopback verifier (lab only)")
    serve.add_argument("--key", required=True)
    serve.add_argument("--port", type=int, default=8765)
    submit = sub.add_parser("submit", help="authenticate and submit synthetic event JSON")
    submit.add_argument("--key", required=True)
    submit.add_argument("--events", required=True)
    submit.add_argument("--port", type=int, default=8765)
    submit.add_argument("--session", default=None)
    manifest = sub.add_parser("manifest", help="hash explicitly selected owned files")
    manifest.add_argument("root")
    manifest.add_argument("paths", nargs="+")
    verify = sub.add_parser("verify", help="compare selected files to a trusted manifest")
    verify.add_argument("root")
    verify.add_argument("manifest")
    imported = sub.add_parser("import-kernel", help="validate our collector JSONL")
    imported.add_argument("path")
    journal = sub.add_parser("journal-check", help="verify an HMAC-chained lab journal")
    journal.add_argument("path")
    journal.add_argument("--key", required=True)
    journal.add_argument("--checkpoint", required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "demo":
            emit(demo())
        elif args.command == "keygen":
            descriptor = os.open(args.path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(secrets.token_bytes(32))
            emit({"created": str(Path(args.path)), "bytes": 32,
                  "note": "Keep the key private; Windows permissions follow the directory ACL."})
        elif args.command == "serve":
            if not 0 <= args.port <= 65535:
                raise ValueError("invalid port")
            with LabHTTPServer(read_key(args.key), args.port) as server:
                print(f"Local laboratory verifier: http://127.0.0.1:{server.server_port}", flush=True)
                try:
                    server.serve_forever()
                except KeyboardInterrupt:
                    pass
        elif args.command == "submit":
            if not 1 <= args.port <= 65535:
                raise ValueError("invalid port")
            key = read_key(args.key)
            source = read_json(args.events)
            events = source.get("events") if type(source) is dict else source
            if type(events) is not list or not 1 <= len(events) <= 4096:
                raise ValueError("expected 1..4096 events")
            session = args.session or secrets.token_hex(16)
            results = []
            # Small batches keep native telemetry under both byte and node caps.
            for offset in range(0, len(events), 32):
                challenge = post(args.port, "/v1/challenge", {"session_id": session})
                envelope = sign_batch(key, challenge, challenge["next_sequence"], events[offset:offset + 32])
                results.append(post(args.port, "/v1/batch", envelope))
            emit({"session_id": session, "results": results})
        elif args.command == "manifest":
            emit(build_manifest(args.root, args.paths))
        elif args.command == "verify":
            result = verify_manifest(args.root, read_json(args.manifest))
            emit(result)
            return 0 if result["ok"] else 2
        elif args.command == "import-kernel":
            emit(read_kernel_log(args.path))
        elif args.command == "journal-check":
            records = read_journal(args.path, read_key(args.key), read_json(args.checkpoint))
            emit({"verified_records": len(records), "checkpoint_checked": True})
    except (ValueError, OSError, TypeError, RecursionError, http.client.HTTPException) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
