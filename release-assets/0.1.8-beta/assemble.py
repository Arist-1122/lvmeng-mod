"""Verify the previous public ZIP and assemble the four new release assets."""

from argparse import ArgumentParser
from hashlib import file_digest, sha1, sha256
from pathlib import Path, PurePosixPath
from tempfile import TemporaryDirectory
from zipfile import ZipFile
import json
import re
import shutil


ROOT = Path(__file__).resolve().parent
TAG = '0.1.8-beta'
MANAGER = f'lvmeng-mod-manager-{TAG}.zip'
SOURCE = f'lvmeng-mod-source-{TAG}.zip'
CHECKSUM = f'{MANAGER}.sha256'
PREVIEW = 'redblack-preview.png'
PREVIEW_MEMBER = 'previews/redblack-combat.png'
PUBLIC_BASE_SIZE = 329019838
PUBLIC_BASE_SHA256 = '47530c7e01ff7830e40808a882f10ea7532c13e89558b69cc02820bc5a2963af'
CARDS = {'litelisi-crossgame-2b', 'litelisi-crossgame-cantarella', 'litelisi-crossgame-kafka',
         'litelisi-crossgame-lyfe', 'litelisi-redblack', 'litelisi-starsea'}
PRIVATE_NAMES = {'logs', 'control', 'user-data', 'vfx-capture', 'game-location.json', 'settings.json',
                 'reshade.ini', 'reshade.log', '.git', 'litelisi-body-blue', 'litelisi-body-red',
                 'litelisi-crimson-vesper', 'gameassembly.dll', 'unityplayer.dll', 'lumin.exe'}
MIB = 1024 * 1024


def require(condition, problem):
    if not condition:
        raise ValueError(problem)


def checked_file(path, expected_bytes, expected_sha256):
    require(path.is_file() and not path.is_symlink() and path.stat().st_size == expected_bytes,
            'Missing file or incorrect byte count: ' + path.name)
    with path.open('rb') as stream:
        require(file_digest(stream, 'sha256').hexdigest() == expected_sha256,
                'Incorrect SHA-256: ' + path.name)


def transfer_range(source, output, start, count):
    require(type(start) == type(count) == int and start >= 0 and count > 0, 'Invalid byte range')
    source.seek(start)
    while count:
        block = source.read(min(count, MIB))
        require(block, 'Unexpected end of transfer data')
        output.write(block)
        count -= len(block)


def validate_zip(file, runtime=False):
    with ZipFile(file) as archive:
        paths = [PurePosixPath(entry) for entry in archive.namelist()]
        seen = set()
        for name, path in zip(archive.namelist(), paths):
            canonical = path.as_posix()
            require(name == canonical and not path.is_absolute() and
                    '\\' not in name and ':' not in name and '..' not in path.parts and
                    not (set(part.casefold() for part in path.parts) & PRIVATE_NAMES),
                    'Unsafe or private ZIP entry: ' + name)
            require(canonical.casefold() not in seen, 'Duplicate ZIP entry: ' + name)
            seen.add(canonical.casefold())
        require(archive.testzip() is None, 'Corrupt ZIP: ' + file.name)
        if runtime:
            cards = {p.parts[1] for p in paths if len(p.parts) == 3 and
                     p.parts[0] == 'mods' and p.parts[2] == 'mod.json'}
            require(cards == CARDS and 'lvmengmodmanager.exe' in seen, 'Unexpected runtime cards')


def assemble(previous, output):
    manifest = json.loads((ROOT / 'manifest.json').read_bytes())
    require(manifest['tag'] == TAG and manifest['releaseMode'] in ('latest', 'prerelease'), 'Unexpected release mode')
    require(re.fullmatch(r'[0-9a-f]{40}', manifest['targetCommit']), 'Invalid release target')
    require(manifest['base'] == {'bytes': PUBLIC_BASE_SIZE, 'sha256': PUBLIC_BASE_SHA256},
            'Base differs from the public 0.16-beta asset')
    checked_file(previous, PUBLIC_BASE_SIZE, PUBLIC_BASE_SHA256)
    require(not output.exists() and output.parent.is_dir(), 'Use a new output directory')
    require((ROOT / 'notes.md').read_bytes().strip(), 'Release notes are required')
    require(manifest['manager']['filename'] == MANAGER and manifest['source']['filename'] == SOURCE and
            manifest['preview']['filename'] == PREVIEW, 'Unexpected upload names')

    with TemporaryDirectory(prefix='release-018-', dir=output.parent) as scratch:
        work = Path(scratch)
        combined = work / 'combined'
        parts = manifest['parts']
        require(0 < len(parts) <= 512, 'Incorrect transfer part count')
        with combined.open('xb') as payload:
            for serial, item in enumerate(parts):
                expected = f'parts/data-{serial:03}.part'
                require(item['path'] == expected and 0 < item['bytes'] <= MIB, 'Invalid transfer part')
                part = ROOT / expected
                checked_file(part, item['bytes'], item['sha256'])
                data = part.read_bytes()
                blob_sha = sha1(f'blob {len(data)}\0'.encode() + data).hexdigest()
                require(blob_sha == item['gitBlobSha1'], 'Unexpected Git blob: ' + expected)
                payload.write(data)

        delivery = work / 'assets'
        delivery.mkdir()
        manager = manifest['manager']
        source = manifest['source']
        data_limit = source['offset']
        require(data_limit >= 0 and combined.stat().st_size == data_limit + source['bytes'],
                'Incorrect concatenated transfer length')
        total = 0
        with (delivery / MANAGER).open('xb') as target, previous.open('rb') as base, combined.open('rb') as delta:
            for kind, start, count in manager['ranges']:
                require(kind in ('base', 'data'), 'Unknown transfer source')
                limit = PUBLIC_BASE_SIZE if kind == 'base' else data_limit
                require(start + count <= limit and total + count <= manager['bytes'], 'Excessive transfer range')
                transfer_range(base if kind == 'base' else delta, target, start, count)
                total += count
        with (delivery / SOURCE).open('xb') as target, combined.open('rb') as delta:
            transfer_range(delta, target, data_limit, source['bytes'])
        preview = manifest['preview']
        require(0 < preview['bytes'] <= 8 * MIB, 'The release preview exceeds 8 MiB')
        require(preview['zipMember'] == PREVIEW_MEMBER, 'Unexpected PNG source member')
        with ZipFile(delivery / MANAGER) as archive:
            require(archive.getinfo(PREVIEW_MEMBER).file_size == preview['bytes'], 'Incorrect PNG member size')
            (delivery / PREVIEW).write_bytes(archive.read(PREVIEW_MEMBER))
        require((delivery / PREVIEW).read_bytes()[:8] == b'\x89PNG\r\n\x1a\n', 'Invalid PNG preview')
        for name, spec in ((MANAGER, manager), (SOURCE, source), (PREVIEW, preview)):
            checked_file(delivery / name, spec['bytes'], spec['sha256'])
        validate_zip(delivery / MANAGER, runtime=True)
        validate_zip(delivery / SOURCE)
        checksum = f'{manager["sha256"]}  {MANAGER}\n'.encode('ascii')
        (delivery / CHECKSUM).write_bytes(checksum)
        assets = [{'name': name, 'bytes': spec['bytes'], 'sha256': spec['sha256']}
                  for name, spec in ((MANAGER, manager), (SOURCE, source), (PREVIEW, preview))]
        assets.append({'name': CHECKSUM, 'bytes': len(checksum), 'sha256': sha256(checksum).hexdigest()})
        delivery.rename(output)
    return {'tag': TAG, 'targetCommit': manifest['targetCommit'], 'releaseMode': manifest['releaseMode'], 'assets': assets}


def verify_upload(expected, remote):
    pinned = {asset['name']: (asset['bytes'], 'sha256:' + asset['sha256'], 'uploaded')
              for asset in json.loads(expected.read_bytes())['assets']}
    actual = json.loads(remote.read_bytes())
    observed = {asset['name']: (asset['size'], asset.get('digest'), asset['state']) for asset in actual}
    require(len(actual) == len(pinned) and pinned == observed, 'GitHub release asset names, byte counts or SHA-256 differ')
    return {'uploadedAssetsVerified': sorted(pinned)}


if __name__ == '__main__':
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--verify-upload', action='store_true')
    parser.add_argument('first', type=Path)
    parser.add_argument('second', type=Path)
    args = parser.parse_args()
    result = verify_upload(args.first, args.second) if args.verify_upload else assemble(args.first, args.second)
    print(json.dumps(result, sort_keys=True))
