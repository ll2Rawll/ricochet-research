#!/usr/bin/env python3
"""Validate focused citation closure, build identity and static evidence pointers."""
import collections
import hashlib
import json
import re
from pathlib import Path
from build_technical import pointer

ROOT=Path(__file__).resolve().parents[1]

def validate():
    raw=(ROOT/'technical/mw4-warzone.jsonl').read_bytes()
    records=[json.loads(l) for l in raw.splitlines()]
    meta=records[0]
    assert meta['type']=='meta' and meta['schema_version']=='mw4-warzone-technical-1.0'
    assert sum(r['type']=='meta' for r in records)==1
    sources={r['id']:r for r in records if r['type']=='source'}
    claims={r['id']:r for r in records if r['type']=='claim'}
    assert len(records)==1+len(sources)+len(claims),'Duplicate IDs or unexpected records'
    assert meta['source_count']==len(sources) and meta['claim_count']==len(claims)
    core_raw=(ROOT/'ricochet.jsonl').read_bytes()
    assert meta['parent_sha256']==hashlib.sha256(core_raw).hexdigest()
    core={r['id']:r for l in core_raw.splitlines() if 'id' in (r:=json.loads(l))}
    core_meta=json.loads(core_raw.splitlines()[0])
    assert meta['revision']==core_meta['revision']
    for id,r in {**sources,**claims}.items():
        if re.fullmatch(r'[CS]\d+',id):assert r==core[id],f'Core record changed: {id}'
    used=set()
    for c in claims.values():
        assert c['sources'] and set(c['sources'])<=sources.keys()
        assert c['status'] in meta['statuses']
        assert c['as_of']<=meta['researched_on']
        for key in ['basis_claims','updates','contrasts_with']:
            assert set(c.get(key,[]))<=claims.keys()
        used.update(c['sources'])
    assert used==sources.keys(),'Uncited focused source'
    artifact=sources['A01'];report_raw=(ROOT/artifact['report']).read_bytes();report=json.loads(report_raw)
    assert artifact['report_sha256']==hashlib.sha256(report_raw).hexdigest()
    assert artifact['archive']==report['archive']
    assert artifact['executable_sha256']==report['executable']['sha256']
    assert artifact['scope_source'] in sources
    for name,data in artifact['components'].items():
        assert data['sha256']==report['components'][name]['sha256']
        assert data['bytes']==report['components'][name]['bytes']
    observations=[r for r in claims.values() if r['status']=='static_observation']
    assert {r['id'] for r in observations}=={f'T{n:03d}' for n in range(1,17)}
    assert meta['static_observation_count']==len(observations)
    assert meta['core_claim_count']==len(claims)-len(observations)
    assert meta['artifact_source_count']==1
    for c in observations:
        v=c['verification']
        assert v['result']=='static_file_content_observed' and v['artifact']=='A01'
        assert v['report']==artifact['report'] and v['evidence']
        assert set(c['sources'])=={'A01','S27'}
        for item in v['evidence']:
            value=pointer(report,item['report_pointer'])
            assert len({'expected','contains','absent'}&item.keys())==1
            if 'expected' in item:assert value==item['expected'],c['id']
            if 'contains' in item:assert all(x in value for x in item['contains']),c['id']
            if 'absent' in item:assert all(x not in value for x in item['absent']),c['id']
    provenance=json.loads((ROOT/'technical/provenance.json').read_text())
    assert provenance['report_sha256']==artifact['report_sha256']
    assert provenance['archive_sha256']==artifact['archive']['sha256']
    assert provenance['archive_bytes']==artifact['archive']['bytes']
    assert provenance['download_url']==artifact['url']
    summary=dict(revision=meta['revision'],sources=len(sources),core_claims=meta['core_claim_count'],
                 static_observations=len(observations),bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),
                 report_sha256=artifact['report_sha256'],statuses=dict(collections.Counter(r['status'] for r in claims.values())))
    return summary

if __name__=='__main__':print(json.dumps(validate(),indent=2))
