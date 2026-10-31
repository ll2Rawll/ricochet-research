#!/usr/bin/env python3
"""Check knowledge-file integrity and evidence boundaries; no dependencies/network."""
import collections
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def validate(path):
    raw = path.read_bytes()
    rows = raw.decode('utf-8').splitlines()
    assert rows and all(rows), 'Blank/empty JSONL'
    records = [json.loads(x) for x in rows]
    assert records[0]['type']=='meta'
    assert sum(x['type']=='meta' for x in records)==1
    m=records[0]
    sources={x['id']:x for x in records if x['type']=='source'}
    claims={x['id']:x for x in records if x['type']=='claim'}
    ids=[x['id'] for x in records if 'id' in x]
    assert len(ids)==len(set(ids)), 'Duplicate IDs'
    assert set(x['type'] for x in records)=={'meta','source','claim'}
    assert m['source_count']==len(sources) and m['claim_count']==len(claims)
    date_pattern=r'\d{4}(?:-\d{2}(?:-\d{2})?)?'
    for s in sources.values():
        assert re.fullmatch(r'S\d{2}',s['id'])
        assert s['url'].startswith('https://')
        assert s['title'] and s['reviewed_sections'] and s['kind']
        assert re.fullmatch(date_pattern,s['published']) if s['published'] else True
        assert s['accessed']<=m['researched_on']
    used=set()
    words=collections.Counter()
    for c in claims.values():
        assert re.fullmatch(r'C\d{3}',c['id'])
        assert c['status'] in m['statuses']
        assert c['text'] and c['scope'] and c['topic'] and c['locator']
        assert re.fullmatch(date_pattern,c['as_of'])
        assert c['as_of']<=m['researched_on']
        assert len(c['sources'])==len(set(c['sources']))
        assert set(c['sources'])<=sources.keys(), f'Unresolved citation {c["id"]}'
        if c['status']=='unknown':
            assert not c['sources'] and c.get('basis_claims'), 'Unknowns must be original audit findings with evidence links'
        else:
            assert c['sources'], f'Unsourced factual claim {c["id"]}'
        for key in ['basis_claims','updates','contrasts_with']:
            assert set(c.get(key,[]))<=claims.keys(), f'Broken {key} reference'
            assert c['id'] not in c.get(key,[])
        for s in c['sources']:
            used.add(s)
            words[s]+=len(c['text'].split())
        if c['status']=='platform_context':
            assert all(sources[s]['kind']=='platform_primary' for s in c['sources'])
    assert used==sources.keys(), f'Unused sources: {sources.keys()-used}'
    assert max(words.values())<=200, f'Overlong source summaries: {dict((k,v) for k,v in words.items() if v>200)}'
    # Dependencies must terminate and must not imply circular corroboration.
    def visit(id, ancestors):
        assert id not in ancestors, 'Circular evidence dependency'
        for dep in claims[id].get('basis_claims',[]): visit(dep, ancestors|{id})
    for id in claims: visit(id,set())
    return dict(schema_version=m['schema_version'],revision=m['revision'],researched_on=m['researched_on'],
                sources=len(sources),claims=len(claims),bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),
                topics=dict(sorted(collections.Counter(c['topic'] for c in claims.values()).items())),
                statuses=dict(sorted(collections.Counter(c['status'] for c in claims.values()).items())))

if __name__=='__main__':
    print(json.dumps(validate(ROOT/'ricochet.jsonl'),indent=2))
