from hashlib import sha256
from pathlib import Path
import json
import sys
from zipfile import ZipFile

root = Path(__file__).resolve().parent
target = Path(sys.argv[1])
m = json.loads((root / 'manifest.json').read_bytes())
archive = target / m['archive']
digest = sha256()
length = 0
with archive.open('wb') as output:
    for part in m['parts']:
        assert Path(part['name']).name == part['name']
        data = (root / part['name']).read_bytes()
        assert len(data) == part['bytes'] and sha256(data).hexdigest() == part['sha256']
        output.write(data)
        digest.update(data)
        length += len(data)
assert length == m['bytes'] and digest.hexdigest() == m['sha256']
with ZipFile(archive) as zipped:
    assert zipped.testzip() is None
    mods = {Path(p).parts[2] for p in zipped.namelist() if Path(p).parts[1:2] == ('mods',)}
    assert mods == set(m['mods']) == {'litelisi-redblack', 'litelisi-starsea'}
    preview = zipped.read(m['previewMember'])
assert sha256(preview).hexdigest() == m['previewSha256']
(target / m['preview']).write_bytes(preview)
(target / (archive.name + '.sha256')).write_text(digest.hexdigest() + '  ' + archive.name + '\n', encoding='ascii')
