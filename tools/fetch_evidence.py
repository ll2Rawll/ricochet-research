#!/usr/bin/env python3
"""Fetch cited primary documents into an ignored local cache for manual review."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import urllib.parse
import urllib.request
from html.parser import HTMLParser
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

class Extract(HTMLParser):
    def __init__(self):
        super().__init__(); self.parts=[]; self.skip=0
    def handle_starttag(self,t,a):
        if t in ('script','style','noscript'):self.skip+=1
        if not self.skip and t in ('p','h1','h2','h3','h4','h5','h6','li','tr','div','section','article','br'):self.parts.append('\n')
    def handle_endtag(self,t):
        if t in ('script','style','noscript'):self.skip=max(0,self.skip-1)
        if not self.skip and t in ('p','h1','h2','h3','h4','h5','h6','li','tr','div','section','article'):self.parts.append('\n')
    def handle_data(self,d):
        if not self.skip:self.parts.append(d)
    def text(self):
        return '\n'.join(re.sub(r'\s+',' ',l).strip() for l in ''.join(self.parts).splitlines() if l.strip())

def fetch(source,out):
    url=source['url']; method='direct_https'
    if 'x.com/' in url:
        url='https://publish.twitter.com/oembed?url='+urllib.parse.quote(url,safe='');method='publisher_oembed'
    if 'github.com/' in url and '/blob/' in url:
        url=url.replace('https://github.com/','https://raw.githubusercontent.com/').replace('/blob/','/')
    req=urllib.request.Request(url,headers={'User-Agent':'Public-source research review'})
    with urllib.request.urlopen(req,timeout=30) as r:
        data=r.read(8*1024*1024);content_type=r.headers.get('Content-Type','');final_url=r.url
    id=source['id']
    if data[:4]==b'%PDF':
        if not shutil.which('pdftotext'):raise RuntimeError('Research PDF extraction needs pdftotext (Poppler).')
        pdf=out/(id+'.pdf');pdf.write_bytes(data)
        plain=out/(id+'.plain.txt')
        subprocess.run(['pdftotext','-layout',str(pdf),str(plain)],check=True)
        text=plain.read_text()
    elif method=='publisher_oembed':
        e=Extract();e.feed(json.loads(data).get('html',''));text=e.text()
    elif 'html' in content_type:
        e=Extract();e.feed(data.decode('utf-8',errors='replace'));text=e.text()
    else:text=data.decode('utf-8',errors='replace')
    lines=[l.strip() for l in text.splitlines() if l.strip()]
    (out/(id+'.txt')).write_text('\n'.join(f'P{i:04d} {l}' for i,l in enumerate(lines,1))+'\n')
    digest=hashlib.sha256(text.encode()).hexdigest()
    result=dict(source_id=id,final_url=final_url,paragraphs=len(lines),
                normalized_text_sha256=digest,
                matches_reviewed_text=digest==source['verification']['normalized_text_sha256'])
    (out/(id+'.check.json')).write_text(json.dumps(result,indent=2)+'\n')
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',action='append',required=True,help='Retained source ID; repeat to fetch multiple')
    p.add_argument('--out',type=Path,default=ROOT/'.evidence-cache')
    a=p.parse_args()
    sources={r['id']:r for l in (ROOT/'ricochet.jsonl').read_text().splitlines() if (r:=json.loads(l))['type']=='source'}
    missing=set(a.source)-sources.keys()
    if missing:p.error('Unknown retained source: '+', '.join(sorted(missing)))
    a.out.mkdir(parents=True,exist_ok=True)
    failed=False
    for id in dict.fromkeys(a.source):
        try:result=fetch(sources[id],a.out)
        except Exception as e:result=dict(source_id=id,error=f'{type(e).__name__}: {e}');failed=True
        print(json.dumps(result))
    if failed:raise SystemExit(1)

if __name__=='__main__':main()
