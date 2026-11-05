from pathlib import Path
import sys,subprocess,os,shutil,json,xml.etree.ElementTree as E,datetime,hashlib,argparse
SCRIPT=Path(__file__).resolve().parent
parser=argparse.ArgumentParser();parser.add_argument('--source',required=True);parser.add_argument('--toolchain',required=True);parser.add_argument('--regenerate-glad',action='store_true');opts=parser.parse_args()
R=Path(opts.toolchain).resolve()
S=Path(opts.source).resolve()
N={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
env=dict(os.environ,PYTHONPATH=str(R/'pydeps'),RESHAPE_PINNED_SOURCE=str(S))
logs=R/'logs'/('generated_'+S.name+'_'+datetime.datetime.now().strftime('%Y%m%dT%H%M%S'));logs.mkdir(parents=True,exist_ok=True)
records=[]
def run(name,args,cwd):
 p=subprocess.run(args,cwd=cwd,env=env,capture_output=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS)
 (logs/(name+'.txt')).write_bytes(p.stdout+p.stderr)
 records.append({'name':name,'args':args,'cwd':str(cwd),'exit':p.returncode})
 (logs/'commands.json').write_text(json.dumps(records,indent=2))
 print(name,p.returncode,(p.stdout+p.stderr).decode('utf-8','replace')[-1800:],flush=True)
 if p.returncode:raise RuntimeError(name+' failed')
g=S/'deps/glad'
if not (g/'target/include/glad/vulkan.h').exists() or opts.regenerate_glad:
 t=E.parse(S/'deps/glad.vcxproj')
 props={x.tag.split('}')[-1]:x.text for x in t.findall('m:PropertyGroup/*',N)}
 for p in (S/'deps/khronos').glob('*.xml'):shutil.copy2(p,g/p.name)
 apis=','.join(props[k] for k in ['GladGLVersion','GladWGLVersion','GladVulkanVersion'])
 exts=','.join(props[k] for k in ['GladGLExtensions','GladWGLExtensions','GladVulkanExtensions']).replace(';',',')
 run('glad',[sys.executable,str(SCRIPT/'run_glad_pinned.py'),'--reproducible','--out-path',str(g/'target'),'--api',apis,'--extensions',exts,'c','--mx'],g)
 # Keep copied pinned XML inputs for provenance; these are ignored generated inputs.
if not (S/'res/version.h').exists():
 run('version',[r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe','-NoProfile','-File',str(S/'tools/update_version.ps1'),'res/version.h','-config','Release','-platform','x64'],S)
t=E.parse(S/'ReShade.vcxproj')
fxc=R/'sdk/microsoft.windows.sdk.cpp/c/bin/10.0.26100.0/x64/fxc.exe'
for x in t.findall('.//m:FxCompile[@Include]',N):
 src=S/x.get('Include');out=src.with_suffix('.cso')
 if out.exists():continue
 kind={'Pixel':'ps','Vertex':'vs','Compute':'cs'}[x.find('m:ShaderType',N).text]
 model=x.find('m:ShaderModel',N);version=model.text if model is not None else '4.0'
 run('fxc_'+src.stem,[str(fxc),'/nologo','/T',kind+'_'+version.replace('.','_'),'/E','main','/O3','/Fo',str(out),str(src)],S)
files=[S/'res/version.h',*sorted((S/'res/shaders').glob('*.cso')),*sorted((g/'target').rglob('*'))]
records=[{'path':str(p),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in files if p.is_file()]
(logs/'output_hashes.json').write_text(json.dumps(records,indent=2))
