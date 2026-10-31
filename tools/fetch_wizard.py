#!/usr/bin/env python3
"""Cache the hash-pinned official diagnostic ZIP without executing its contents."""
import argparse
import hashlib
import json
import os
import tempfile
import urllib.request
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
LIMIT=128*1024*1024

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=ROOT/'.evidence-cache/CODSecureAttestationWizard_1.3.0.zip')
    a=p.parse_args()
    provenance=json.loads((ROOT/'technical/provenance.json').read_text())
    expected=provenance['archive_sha256']
    if a.out.exists():
        if a.out.stat().st_size>LIMIT or hashlib.sha256(a.out.read_bytes()).hexdigest()!=expected:
            p.exit(2,'Existing output differs from the reviewed artifact; choose another path.\n')
        print(json.dumps(dict(path=str(a.out),sha256=expected,status='existing_hash_matches')))
        return
    a.out.parent.mkdir(parents=True,exist_ok=True)
    temp=None
    try:
        digest=hashlib.sha256();count=0
        request=urllib.request.Request(provenance['download_url'],headers={'User-Agent':'Public-source artifact review'})
        with urllib.request.urlopen(request,timeout=45) as response, tempfile.NamedTemporaryFile(dir=a.out.parent,delete=False) as out:
            temp=Path(out.name)
            while chunk:=response.read(1024*1024):
                count+=len(chunk)
                if count>LIMIT:raise ValueError('Download exceeds limit')
                digest.update(chunk);out.write(chunk)
        if count!=provenance['archive_bytes'] or digest.hexdigest()!=expected:
            raise ValueError('Downloaded bytes differ from the reviewed release; review required')
        os.replace(temp,a.out);temp=None
        print(json.dumps(dict(path=str(a.out),sha256=expected,bytes=count,status='download_hash_matches')))
    finally:
        if temp is not None:temp.unlink(missing_ok=True)

if __name__=='__main__':main()
