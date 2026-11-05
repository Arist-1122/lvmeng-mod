"""Make a portable Mod folder from a completed, verified runtime package."""
from pathlib import Path
import argparse, hashlib, json, shutil

def child(root, relative):
    result = (root / relative).resolve()
    if not result.is_relative_to(root.resolve()): raise ValueError('Escaping package path')
    return result

def stage(source, destination, mod_id, name, character, skin):
    manifest = json.loads((source/'package.json').read_text(encoding='utf-8-sig'))
    if destination.exists(): raise ValueError('Destination already exists')
    for row in manifest['files']:
        path = child(source, row['path'])
        if hashlib.sha256(path.read_bytes()).hexdigest() != row['sha256']: raise ValueError('Hash mismatch: ' + row['path'])
    if any(Path(row['path']).is_absolute() for row in manifest['gameSamples']): raise ValueError('Client samples must be relative before staging')
    destination.mkdir(parents=True); runtime = destination/'runtime'; runtime.mkdir()
    for name_path in [row['path'] for row in manifest['files']] + ['package.json']:
        output = child(runtime, name_path); output.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(child(source, name_path), output)
    mod = dict(version=1,id=mod_id,name=name,characterId=character,skinId=skin,runtimeDirectory='runtime',entryPoint='LvmengModelTool.exe',packageManifest='package.json')
    if character == 'role_26': mod['characterName'] = '莉特莉丝'
    if skin == 'role_26_02': mod['skinName'] = '绮夜寐影'
    (destination/'mod.json').write_text(json.dumps(mod,ensure_ascii=False,indent=2),encoding='utf-8')
    return mod

if __name__ == '__main__':
    p=argparse.ArgumentParser();p.add_argument('runtime',type=Path);p.add_argument('destination',type=Path);p.add_argument('--id',default='litelisi-redblack');p.add_argument('--name',default='莉特莉丝 · 红黑收尾修正版');p.add_argument('--character',default='role_26');p.add_argument('--skin',default='role_26_02');a=p.parse_args()
    print(json.dumps(stage(a.runtime.resolve(),a.destination.resolve(),a.id,a.name,a.character,a.skin),ensure_ascii=False))
