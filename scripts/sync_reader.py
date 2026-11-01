#!/usr/bin/env python3
"""Refresh the reader snapshot and initial counters from the research corpus."""

import argparse
import json
import re
from datetime import date
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
READER = ROOT / "reader"


def replace_once(text, pattern, replacement):
    updated, count = re.subn(pattern, lambda match: replacement(match), text)
    if count != 1:
        raise ValueError(f"Expected one matching reader field: {pattern}")
    return updated


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Fail if generated reader data needs updating")
    args = parser.parse_args()
    records = [json.loads(line) for line in (ROOT / "ricochet.jsonl").read_text(encoding="utf-8").splitlines()]
    meta = records[0]
    if meta.get("type") != "meta":
        raise ValueError("The corpus must start with metadata")
    for kind, field in (("claim", "claims"), ("source", "sources")):
        if sum(record.get("type") == kind for record in records) != meta[field]:
            raise ValueError(f"Corpus {field} count mismatch")
    snapshot = json.dumps(records, ensure_ascii=False, separators=(",", ":")) + "\n"
    app = (READER / "app.js").read_text(encoding="utf-8")
    app = replace_once(app, r"const SNAPSHOT_REVISION = \d+;", lambda _: f"const SNAPSHOT_REVISION = {meta['revision']};")
    html = (READER / "index.html").read_text(encoding="utf-8")
    reviewed = date.fromisoformat(meta["reviewed"])
    months = ("JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC")
    values = {
        "total-findings": str(meta["claims"]),
        "total-sources": str(meta["sources"]),
        "findings-tab-count": str(meta["claims"]),
        "sources-tab-count": str(meta["sources"]),
        "revision": f"Research revision {meta['revision']}",
        "reviewed": f"{reviewed.day} {months[reviewed.month - 1]} {reviewed.year}",
    }
    for field, value in values.items():
        html = replace_once(html, rf'(id="{field}">)[^<]*(</)', lambda match, value=value: match[1] + value + match[2])
    outputs = {READER / "data.json": snapshot, READER / "app.js": app, READER / "index.html": html}
    changed = [path for path, content in outputs.items() if not path.exists() or path.read_bytes() != content.encode("utf-8")]
    if args.check:
        if changed:
            parser.exit(1, "Reader needs synchronization: " + ", ".join(str(path.relative_to(ROOT)) for path in changed) + "\n")
    else:
        for path in changed:
            path.write_text(outputs[path], encoding="utf-8")
    print(f"Reader synchronized: revision {meta['revision']}, {meta['claims']} findings, {meta['sources']} sources.")


if __name__ == "__main__":
    main()
