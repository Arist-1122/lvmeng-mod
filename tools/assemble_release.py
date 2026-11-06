"""Reassemble the reviewed archive from transport-sized parts; no dependencies."""
from pathlib import Path
import argparse, hashlib, json

ROOT=Path(__file__).resolve().parent.parent

def assemble(output):
    manifest=json.loads((ROOT/'distribution/manifest.json').read_text(encoding='utf-8'))
    output=output.resolve();output.parent.mkdir(parents=True,exist_ok=True)
    total=hashlib.sha256();size=0
    with output.open('wb') as target:
        for row in manifest['parts']:
            source=(ROOT/row['path']).resolve()
            if not source.is_relative_to(ROOT):raise ValueError('Part path outside repository')
            data=source.read_bytes()
            if len(data)!=row['bytes'] or hashlib.sha256(data).hexdigest()!=row['sha256']:raise ValueError('Part checksum failed: '+row['path'])
            target.write(data);total.update(data);size+=len(data)
    if total.hexdigest()!=manifest['sha256'] or size!=manifest['bytes']:raise ValueError('Complete archive checksum failed')
    print(json.dumps(dict(passed=True,bytes=size,sha256=total.hexdigest(),output=str(output))))
    return manifest

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,default=ROOT/'.local/lvmeng-mod-manager-v0.1.2-preview.zip')
    assemble(parser.parse_args().output)
