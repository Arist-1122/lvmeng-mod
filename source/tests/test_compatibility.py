"""Small non-executed PE fixtures and independent binary manifest mutations."""
from pathlib import Path
import copy,hashlib,json,os,shutil,struct,subprocess,zlib

ROOT=Path(__file__).resolve().parent.parent
SOURCE=ROOT/'source'if(ROOT/'source/GameCompatibility.cs').is_file()else ROOT/'common'
WORK=ROOT/'fixture';CLIENT=WORK/'client';PACKAGE=WORK/'runtime';TOOLS=ROOT/'test-tools'
for p in(WORK,CLIENT,PACKAGE,TOOLS):p.mkdir(parents=True,exist_ok=True)
CSC=Path(os.environ.get('WINDIR','C:/Windows'))/'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
def compile_cs(output,files,kind='exe'):
    p=subprocess.run([str(CSC),'/nologo','/platform:x64','/optimize+','/target:'+kind,'/out:'+str(output),'/r:System.Web.Extensions.dll',*[str(f)for f in files]],capture_output=True,text=True);assert p.returncode==0,p.stdout+p.stderr
compile_cs(ROOT/'CompatibilityCheck.exe',[SOURCE/'GameCompatibility.cs',SOURCE/'CompatibilityCheck.cs'])
for branch in('2022.3','2023.1'):
    src=TOOLS/('Fixture'+branch+'.cs');src.write_text('using System.Reflection;[assembly:AssemblyFileVersion("'+branch+'.99.0")][assembly:AssemblyInformationalVersion("'+branch+'.99f1 (fixture)")] public class NotExecuted {}',encoding='utf-8')
    compile_cs(TOOLS/('Fixture'+branch+'.dll'),[src],'library')
for name in('Lumin.exe','UnityPlayer.dll','GameAssembly.dll','FairGuardProtect.dll'):shutil.copyfile(TOOLS/'Fixture2022.3.dll',CLIENT/name)
(CLIENT/'Lumin_Data').mkdir(exist_ok=True);(CLIENT/'Lumin_Data/app.info').write_text('zhuziplay\nLumin',encoding='utf-8')
bundles=[]
for index in range(3):
    guid=('%032x'%(index+1));data=('render-anchor-'+str(index)).encode();path=CLIENT/'Lumin_Data/yoo/Assets/BundleFiles'/guid[:2]/guid/'__data';path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    bundles.append(dict(name='bundle-'+str(index),guid=guid,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
datafile=CLIENT/'Lumin_Data/data.unity3d';datafile.write_bytes(b'fixed-main-data')
S=lambda value:struct.pack('<H',len(value.encode()))+value.encode()
def manifest(version,bundle_rows=None,bad_dep=False,tail=False):
    rows=bundles if bundle_rows is None else bundle_rows
    data=struct.pack('<I',0x00594f4f)+S('2.3.1')+bytes([0,0,0])+struct.pack('<ii',0,0)+S('pipeline')+S('Assets')+S('prod_1.0.0_prod.w_'+str(version))+S('fixture')
    data+=struct.pack('<i',1)+S('address')+S('assets/test')+S('guid')+struct.pack('<H',0)+struct.pack('<iH',0,0)+struct.pack('<i',len(rows))
    for row in rows:
        data+=S(row['name'])+struct.pack('<I',0)+S(row['guid'])+S('crc')+struct.pack('<qB',row['bytes'],0)+struct.pack('<H',0)+S('extra')+struct.pack('<H',1 if bad_dep else 0)+(struct.pack('<i',999)if bad_dep else b'')
    return data+(b'x'if tail else b'')
M=CLIENT/'Lumin_Data/yoo/Assets/ManifestFiles';M.mkdir(parents=True,exist_ok=True)
def put(name,data):
    path=M/(name+'.bytes');path.write_bytes(data);path.with_suffix('.hash').write_text(struct.pack('<I',zlib.crc32(data)).hex(),encoding='ascii')
put('Assets_prod_1.0.0_prod.w_1',manifest(10));put('Assets_prod_1.0.0_prod.w_2',manifest(20))
metadata=json.loads((SOURCE/'package_metadata.json').read_text(encoding='utf-8'));policy=metadata['compatibilityPolicy'];policy['manifest']['requiredBundles']=copy.deepcopy(bundles)
anchors=[dict(path='Lumin_Data/data.unity3d',bytes=datafile.stat().st_size,sha256=hashlib.sha256(datafile.read_bytes()).hexdigest())]
for row in bundles:anchors.append(dict(path='Lumin_Data/yoo/Assets/BundleFiles/'+row['guid'][:2]+'/'+row['guid']+'/__data',bytes=row['bytes'],sha256=row['sha256']))
policy['renderAnchorSets']=[dict(id='fixture-render',files=anchors)]
profiles=[dict(id='fixture-known',label='not executed',samples=[dict(path=name,sha256=hashlib.sha256((CLIENT/name).read_bytes()).hexdigest())for name in('Lumin.exe','UnityPlayer.dll','GameAssembly.dll','FairGuardProtect.dll')])]
BASE=dict(knownProfiles=profiles,compatibilityPolicy=policy)
def save_policy(value): (PACKAGE/'package.json').write_text(json.dumps(value),encoding='utf-8')
save_policy(BASE)
snapshot={p.relative_to(CLIENT):p.read_bytes()for p in CLIENT.rglob('*')if p.is_file()}
def restore():
    for p in list(CLIENT.rglob('*')):
        if p.is_file()and p.relative_to(CLIENT)not in snapshot:p.unlink()
    for path,data in snapshot.items():(CLIENT/path).parent.mkdir(parents=True,exist_ok=True);(CLIENT/path).write_bytes(data)
    save_policy(BASE)
def inspect():
    p=subprocess.run([str(ROOT/'CompatibilityCheck.exe'),'--check-compatibility',str(PACKAGE),str(CLIENT/'Lumin.exe')],capture_output=True,encoding='utf-8');return json.loads(p.stdout)
results=[]
def case(name,change,allowed,state=None):
    restore();change();r=inspect();passed=r['allowed']==allowed and(state is None or r['state']==state);results.append(dict(name=name,passed=passed,state=r['state'],message=r['message']if not passed else None));assert passed,(name,r)
case('Known full samples still pass all render gates',lambda:None,True,'known-profile')
case('EXE update with stable render accepted',lambda:(CLIENT/'Lumin.exe').write_bytes(snapshot[Path('Lumin.exe')]+b'updated'),True,'compatible-update')
case('IL2CPP update with stable render accepted',lambda:(CLIENT/'GameAssembly.dll').write_bytes(snapshot[Path('GameAssembly.dll')]+b'updated'),True,'compatible-update')
case('Wrong app identity rejects even known PE set',lambda:(CLIENT/'Lumin_Data/app.info').write_text('other\nLumin'),False)
def pe32():
    p=CLIENT/'Lumin.exe';data=bytearray(p.read_bytes());at=struct.unpack_from('<I',data,0x3c)[0];struct.pack_into('<H',data,at+4,0x14c);p.write_bytes(data)
case('Non AMD64 rejects',pe32,False)
case('Unsupported Unity branch rejects',lambda:shutil.copyfile(TOOLS/'Fixture2023.1.dll',CLIENT/'UnityPlayer.dll'),False)
case('Missing critical engine rejects',lambda:(CLIENT/'GameAssembly.dll').unlink(),False)
case('Missing required protection file rejects',lambda:(CLIENT/'FairGuardProtect.dll').unlink(),False)
case('Changed main render anchor rejects even known samples',lambda:datafile.write_bytes(b'changed'),False)
def wrong_guid():
    rows=copy.deepcopy(bundles);rows[0]['guid']='f'*32;put('Assets_prod_1.0.0_prod.w_3',manifest(30,rows))
case('Newest manifest source changed with old cache remaining rejects',wrong_guid,False)
case('Highest bad CRC does not fall back',lambda:(M/'Assets_prod_1.0.0_prod.w_2.bytes').write_bytes(manifest(20)+b'bad'),False)
case('Missing highest sidecar rejects',lambda:(M/'Assets_prod_1.0.0_prod.w_2.hash').unlink(),False)
case('Unknown future manifest family rejects',lambda:put('Assets_prod_1.1.0_prod.w_3',manifest(30)),False)
case('Trailing manifest data rejects despite valid CRC',lambda:put('Assets_prod_1.0.0_prod.w_3',manifest(30,tail=True)),False)
case('Invalid dependency rejects despite valid CRC',lambda:put('Assets_prod_1.0.0_prod.w_3',manifest(30,bad_dep=True)),False)
def head_order():
    rows=copy.deepcopy(bundles);rows[0]['guid']='f'*32;put('Assets_prod_1.0.0_prod.w_99',manifest(5,rows))
case('Header package version outranks cache counter',head_order,True,'known-profile')
case('Older body CRC damage does not block valid newest header',lambda:(M/'Assets_prod_1.0.0_prod.w_1.bytes').write_bytes(manifest(10)+b'old-bad-body'),True,'known-profile')
case('Older missing hash does not block valid newest manifest',lambda:(M/'Assets_prod_1.0.0_prod.w_1.hash').unlink(),True,'known-profile')
case('Unrankable truncated header fails closed with file diagnostic',lambda:(M/'Assets_prod_1.0.0_prod.w_3.bytes').write_bytes(b'bad'),False)
case('Absent policy fails closed',lambda:save_policy(dict(knownProfiles=profiles)),False)
case('Empty anchor groups fail closed',lambda:save_policy(dict(knownProfiles=profiles,compatibilityPolicy=dict(policy,renderAnchorSets=[]))),False)
case('Anchor path escape fails closed',lambda:save_policy(dict(knownProfiles=profiles,compatibilityPolicy=dict(policy,renderAnchorSets=[dict(id='bad',files=[dict(path='../outside',sha256='0'*64,bytes=1)])]))),False)
restore()
report=dict(passed=all(x['passed']for x in results),tests=results,fixtureBytes=sum(len(x)for x in snapshot.values()),gameCompatibilitySha256=hashlib.sha256((SOURCE/'GameCompatibility.cs').read_bytes()).hexdigest(),gameLaunched=False,fixtureExecutablesExecuted=False,clientDllLoaded=False)
(ROOT/'fixture_tests.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(dict(passed=report['passed'],tests=len(results),fixtureBytes=report['fixtureBytes'])))
