#!/usr/bin/env python3
"""Retrieve compact, citation-complete JSONL subsets. Standard library only."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--file', type=Path, default=ROOT / 'ricochet.jsonl')
    p.add_argument('--topic', action='append', default=[], help='Exact topic; repeat for OR')
    p.add_argument('--status', action='append', default=[], help='Exact status; repeat for OR')
    p.add_argument('--id', action='append', default=[], help='Claim/observation ID; repeat for OR')
    p.add_argument('--search', default='', help='Case-insensitive substring in claim text/scope')
    p.add_argument('--limit', type=int, help='Maximum directly matched claims; dependencies are additional')
    a = p.parse_args()
    if a.limit is not None and a.limit < 1:
        p.error('--limit must be positive')
    raw = a.file.read_bytes()
    records = [json.loads(x) for x in raw.decode('utf-8').splitlines()]
    meta = next(x for x in records if x['type'] == 'meta').copy()
    claims = {x['id']: x for x in records if x['type'] == 'claim'}
    sources = {x['id']: x for x in records if x['type'] == 'source'}
    for provided, available, label in [(a.topic,{x['topic'] for x in claims.values()},'topic'), (a.status,set(meta['statuses']),'status'), (a.id,set(claims),'claim ID')]:
        missing = set(provided) - available
        if missing:
            p.error(f'Unknown {label}: {", ".join(sorted(missing))}')
    matches = [c for c in claims.values()
               if (not a.topic or c['topic'] in a.topic)
               and (not a.status or c['status'] in a.status)
               and (not a.id or c['id'] in a.id)
               and a.search.casefold() in (c['text']+' '+c['scope']).casefold()]
    if a.limit:
        matches = matches[:a.limit]
    selected = {c['id'] for c in matches}
    # Include evidence and historical cross-links so subsets have no dangling IDs.
    pending = list(selected)
    while pending:
        c = claims[pending.pop()]
        for dep in c.get('basis_claims', []) + c.get('updates', []) + c.get('contrasts_with', []):
            if dep not in selected:
                selected.add(dep)
                pending.append(dep)
    used = {s for id in selected for s in claims[id]['sources']}
    meta.update(subset=True, parent_sha256=hashlib.sha256(raw).hexdigest(),
                matched_claim_ids=[c['id'] for c in matches],
                claim_count=len(selected), source_count=len(used))
    if meta['schema_version']=='mw4-warzone-technical-1.0':
        static_count=sum(claims[id]['status']=='static_observation' for id in selected)
        meta.update(core_claim_count=len(selected)-static_count,
                    static_observation_count=static_count,
                    artifact_source_count=sum(sources[id]['kind']=='vendor_diagnostic_artifact' for id in used))
    emit = [meta] + [s for id,s in sources.items() if id in used] + [c for id,c in claims.items() if id in selected]
    for item in emit:
        print(json.dumps(item,ensure_ascii=False,separators=(',',':')))

if __name__ == '__main__':
    main()
