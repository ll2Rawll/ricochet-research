#!/usr/bin/env python3
"""Rebuild the bibliography, byte/topic index, and integrity manifest."""
import collections
import json
from pathlib import Path
from validate import validate

ROOT=Path(__file__).resolve().parents[1]
summary=validate(ROOT/'ricochet.jsonl')
records=[]
offset=0
entries={}
topics=collections.defaultdict(list)
for line_number,line in enumerate((ROOT/'ricochet.jsonl').read_bytes().splitlines(keepends=True),1):
    r=json.loads(line)
    records.append(r)
    if 'id' in r:
        entries[r['id']]={'line':line_number,'byte_offset':offset,'byte_length':len(line)}
    if r['type']=='claim':topics[r['topic']].append(r['id'])
    offset+=len(line)
index=dict(knowledge_file='ricochet.jsonl',sha256=summary['sha256'],
           offset_unit='UTF-8 bytes; zero-based; length includes newline',
           topics=dict(sorted(topics.items())),records=entries)
(ROOT/'index.json').write_text(json.dumps(index,ensure_ascii=False,indent=2)+'\n')
(ROOT/'manifest.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
lines=['# Source bibliography','',
       'The complete source metadata and reviewed-section locators are in [ricochet.jsonl](ricochet.jsonl). Dates below are publication/update dates when established; access date is 2026-10-31. Primary vendor statements are not independent verification.','',
       '| ID | Date | Source | Kind |','| --- | --- | --- | --- |']
for s in sorted((r for r in records if r['type']=='source'),key=lambda s:s['id']):
    title=s['title'].replace('|','/')
    lines.append(f'| {s["id"]} | {s["published"] or "not established"} | [{title}]({s["url"]}) | {s["kind"]} |')
(ROOT/'SOURCES.md').write_text('\n'.join(lines)+'\n')
print(f'Indexed {summary["claims"]} claims and {summary["sources"]} sources; {summary["bytes"]} bytes.')
