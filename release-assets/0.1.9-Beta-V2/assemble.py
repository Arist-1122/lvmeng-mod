"""Reconstruct the reviewed release from the previous public ZIPs and the delta."""

from argparse import ArgumentParser
from contextlib import ExitStack
from hashlib import file_digest, sha1
from pathlib import Path
from tempfile import TemporaryDirectory
from zipfile import ZipFile
import json
import re


ROOT = Path(__file__).resolve().parent
TAG = '0.1.9-Beta-V2'
BASES = {
    'runtime': {'bytes': 335625774, 'sha256': '35162ab0a56802f38b28763ab867df5d037db7c179e6619e37c9036cc6de3f5e'},
    'source': {'bytes': 694015, 'sha256': '3acc85ee34078bd75527f0f098aab8b009ef6007fd3edb3173d17be24e6bf79b'},
}
MANAGER = f'lvmeng-mod-manager-{TAG}.zip'
SOURCE = f'lvmeng-mod-source-{TAG}.zip'
NAMES = {name for archive in (MANAGER, SOURCE) for name in (archive, archive + '.sha256')}


def digest(path):
    with path.open('rb') as stream:
        return file_digest(stream, 'sha256').hexdigest()


def check_file(path, item):
    assert path.is_file() and not path.is_symlink()
    assert path.stat().st_size == item['bytes'] and digest(path) == item['sha256'], path.name


def transfer(source, target, start, size, limit):
    assert type(start) is type(size) is int and 0 <= start < start + size <= limit
    source.seek(start)
    while size:
        block = source.read(min(1024 * 1024, size))
        assert block
        target.write(block)
        size -= len(block)


def assemble(base_dir, output):
    data = json.loads((ROOT / 'manifest.json').read_bytes())
    assert data['tag'] == TAG and data['bases'] == BASES
    assert re.fullmatch(r'[0-9a-f]{40}', data['targetCommit'])
    assert {row['name'] for row in data['assets']} == NAMES and len(data['assets']) == len(NAMES)
    check_file(ROOT / 'notes.md', data['notes'])
    for kind, expected in BASES.items():
        check_file(base_dir / (kind + '.zip'), expected)
    output.mkdir()
    with TemporaryDirectory(prefix='release-019v2-', dir=output.parent) as temp, ExitStack() as resources:
        combined = Path(temp) / 'delta'
        with combined.open('xb') as target:
            for index, row in enumerate(data['parts']):
                assert row['path'] == f'parts/data-{index:03}.part' and 0 < row['bytes'] <= 512 * 1024
                part = ROOT / row['path']
                check_file(part, row)
                value = part.read_bytes()
                assert sha1(f'blob {len(value)}\0'.encode() + value).hexdigest() == row['gitBlobSha1']
                target.write(value)
        ranges = {key: (resources.enter_context((base_dir / (key + '.zip')).open('rb')), item['bytes'])
                  for key, item in BASES.items()}
        ranges['data'] = (resources.enter_context(combined.open('rb')), combined.stat().st_size)
        for row in data['assets']:
            path = output / row['name']
            assert sum(length for _, _, length in row['ranges']) == row['bytes']
            with path.open('xb') as target:
                for kind, start, length in row['ranges']:
                    source, limit = ranges[kind]
                    transfer(source, target, start, length, limit)
            check_file(path, row)
    for name in (MANAGER, SOURCE):
        with ZipFile(output / name) as archive:
            assert archive.testzip() is None
            if name == MANAGER:
                assert archive.read('VERSION.txt').decode('utf-8-sig').strip() == TAG
        assert (output / (name + '.sha256')).read_text().split()[0] == digest(output / name)
    return {'tag': TAG, 'targetCommit': data['targetCommit'],
            'assets': [{key: row[key] for key in ('name', 'bytes', 'sha256')} for row in data['assets']]}


def verify_upload(expected, remote):
    wanted = {row['name']: (row['bytes'], 'sha256:' + row['sha256'], 'uploaded')
              for row in json.loads(expected.read_bytes())['assets']}
    actual = json.loads(remote.read_bytes())
    assert len(actual) == len(wanted)
    assert wanted == {row['name']: (row['size'], row.get('digest'), row['state']) for row in actual}
    return {'uploadedAssetsVerified': sorted(wanted)}


if __name__ == '__main__':
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--verify-upload', action='store_true')
    parser.add_argument('first', type=Path)
    parser.add_argument('second', type=Path)
    args = parser.parse_args()
    print(json.dumps(verify_upload(args.first, args.second) if args.verify_upload else assemble(args.first, args.second)))
