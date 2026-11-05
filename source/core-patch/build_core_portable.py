"""Bounded portable x64 Release build of pinned, unmodified ReShade sources.
Reads repository vcxproj/props metadata; no MSBuild installation or global env edits.
"""
from pathlib import Path
import os,sys,re,json,hashlib,subprocess,datetime,threading,ctypes,argparse,msvcrt,shutil,xml.etree.ElementTree as E
from concurrent.futures import ThreadPoolExecutor,as_completed
parser=argparse.ArgumentParser();parser.add_argument('--source',required=True);parser.add_argument('--toolchain',required=True);parser.add_argument('--output',default='head_bc7_upload_v1');parser.add_argument('--work',default=str(Path(__file__).resolve().parent/'core-build'));opts=parser.parse_args()
R=Path(opts.toolchain).resolve();WORK=Path(opts.work).resolve();WORK.mkdir(parents=True,exist_ok=True);(WORK/'logs').mkdir(exist_ok=True)
S=Path(opts.source).resolve()
if not(S/'ReShade.vcxproj').is_file():raise RuntimeError('Expected patched ReShade source')
if not re.fullmatch(r'[A-Za-z0-9_-]+',opts.output):raise RuntimeError('Simple output label required')
buildlock=(WORK/'build.lock').open('a+b');buildlock.seek(0)
if buildlock.read(1)==b'':buildlock.write(b'0');buildlock.flush()
buildlock.seek(0)
try:msvcrt.locking(buildlock.fileno(),msvcrt.LK_NBLCK,1)
except OSError:raise RuntimeError('Another core build is running; total concurrency cannot exceed 2')
VC=R/'msvc/VC/Tools/MSVC/14.44.35207'
SDK=R/'sdk/microsoft.windows.sdk.cpp/c'
LIB=R/'sdk/microsoft.windows.sdk.cpp.x64/c'
BIN=VC/'bin/Hostx64/x64'
OUT=WORK/'build'/opts.output;OUT.mkdir(parents=True,exist_ok=True)
ATT=WORK/'logs'/('core_'+datetime.datetime.now().strftime('%Y%m%dT%H%M%S'));ATT.mkdir()
NS={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
V={'Configuration':'Release','Platform':'x64','PlatformToolset':'v143','VisualStudioVersion':'17.0','SolutionDir':str(S)+'\\'}
FLAGS=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
git=shutil.which('git')
if not git:raise RuntimeError('Git is required')
head=subprocess.check_output([git,'-C',str(S),'rev-parse','HEAD'],text=True).strip()
diff=subprocess.check_output([git,'-C',str(S),'diff','--binary','HEAD'])
lock=threading.Lock();status={'started':datetime.datetime.now().astimezone().isoformat(),'sourceCommit':head,'sourceRoot':str(S),'outputLabel':opts.output,'maxCompileParallel':2,'priority':'BelowNormal','jobMemoryLimitBytes':6*1024**3,'trackedSourceDiffPresent':bool(diff),'trackedSourceDiffSha256':hashlib.sha256(diff).hexdigest(),'buildDriver':'vcxproj metadata translated to explicit cl/link invocation','steps':[]}
(ATT/'source_diff.patch').write_bytes(diff)
treehash=hashlib.sha256()
for folder in ['include','source','deps','res']:
 for p in sorted((S/folder).rglob('*')):
  if p.is_file() and p.suffix.lower() in {'.h','.hpp','.hxx','.inl','.props','.vcxproj'}:
   treehash.update(str(p.relative_to(S)).encode());treehash.update(p.read_bytes())
headerSignature=treehash.digest();status['headerTreeSha256']=treehash.hexdigest()
class MEM(ctypes.Structure):
 _fields_=[('length',ctypes.c_ulong),('load',ctypes.c_ulong)]+[(n,ctypes.c_ulonglong) for n in ['totalPhys','availPhys','totalPage','availPage','totalVirtual','availVirtual','availExtended']]
def free_ram():
 m=MEM();m.length=ctypes.sizeof(m);ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m));return m.availPhys
class BASIC(ctypes.Structure):
 _fields_=[('user',ctypes.c_longlong),('jobuser',ctypes.c_longlong),('flags',ctypes.c_ulong),('minwork',ctypes.c_size_t),('maxwork',ctypes.c_size_t),('active',ctypes.c_ulong),('affinity',ctypes.c_size_t),('priority',ctypes.c_ulong),('sched',ctypes.c_ulong)]
class IO(ctypes.Structure):_fields_=[(n,ctypes.c_ulonglong) for n in ['readop','writeop','otherop','readbytes','writebytes','otherbytes']]
class EXT(ctypes.Structure):_fields_=[('basic',BASIC),('io',IO),('processMemory',ctypes.c_size_t),('jobMemory',ctypes.c_size_t),('peakProcess',ctypes.c_size_t),('peakJob',ctypes.c_size_t)]
k=ctypes.windll.kernel32;k.CreateJobObjectW.restype=ctypes.c_void_p
k.SetInformationJobObject.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p,ctypes.c_ulong]
k.AssignProcessToJobObject.argtypes=[ctypes.c_void_p,ctypes.c_void_p]
job=k.CreateJobObjectW(None,None);lim=EXT();lim.basic.flags=0x2000|0x200;lim.jobMemory=6*1024**3
if not job or not k.SetInformationJobObject(job,9,ctypes.byref(lim),ctypes.sizeof(lim)):raise RuntimeError('Could not set bounded job')
status['freeRamAtStart']=free_ram()
if status['freeRamAtStart']<8*1024**3:raise RuntimeError('Less than 8 GiB RAM available at start')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save():
 (ATT/'result.json').write_text(json.dumps(status,indent=2),encoding='utf-8')
 (WORK/'logs/core_latest.json').write_text(json.dumps({'attempt':str(ATT),**status},indent=2),encoding='utf-8')
def run(name,args,cwd=S,timeout=180):
 if free_ram()<6*1024**3:raise RuntimeError('Free RAM below 6 GiB: '+name)
 t=datetime.datetime.now().astimezone().isoformat()
 p=subprocess.Popen(args,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,creationflags=FLAGS)
 if not k.AssignProcessToJobObject(job,int(p._handle)):
  p.terminate();p.wait();raise RuntimeError('Could not attach bounded child job')
 try:b,_=p.communicate(timeout=timeout)
 except subprocess.TimeoutExpired:p.kill();b,_=p.communicate();raise RuntimeError('Timed out: '+name)
 log=ATT/(name+'.txt');log.write_bytes(b)
 item={'name':name,'start':t,'args':args,'cwd':str(cwd),'exit':p.returncode,'log':str(log)}
 with lock:
  status['steps'].append(item);save();print(name,p.returncode,flush=True)
 if p.returncode:print(b.decode('utf-8','replace')[-6000:],flush=True)
 return p.returncode
def expand(s):
 for key,val in V.items():s=s.replace('$('+key+')',val)
 return s
def cond(s):
 if not s:return True
 s=expand(s)
 # Deliberately only recognize the literal conditions used in these pinned projects.
 groups=re.split(r'\s+Or\s+',s,flags=re.I)
 vals=[]
 for group in groups:
  atoms=re.split(r'\s+And\s+',group,flags=re.I);b=[]
  for a in atoms:
   m=re.fullmatch(r"\s*'([^']*)'\s*(==|!=|>=)\s*'([^']*)'\s*",a)
   if not m:raise RuntimeError('Unsupported metadata condition: '+s)
   x,op,y=m.groups();b.append(x==y if op=='==' else x!=y if op=='!=' else x>=y)
  vals.append(all(b))
 return any(vals)
def merge_meta(dst,group):
 for node in group:
  if not cond(node.get('Condition')):continue
  key=node.tag.split('}')[-1];value=expand(node.text or '')
  dst[key]=value.replace('%('+key+')',dst.get(key,''))
def parse(project):
 states={};t=E.parse(project)
 def walk(path):
  root=E.parse(path).getroot()
  for node in root:
   tag=node.tag.split('}')[-1]
   if tag=='ImportGroup':imports=node.findall('m:Import',NS)
   elif tag=='Import':imports=[node]
   else:imports=[]
   for x in imports:
    pathtext=expand(x.get('Project',''))
    if '$(' in pathtext:continue
    p=(path.parent/pathtext).resolve()
    if p.is_file():walk(p)
   if tag=='ItemDefinitionGroup' and cond(node.get('Condition')):
    for g in node:merge_meta(states.setdefault(g.tag.split('}')[-1],{}),g)
 walk(project)
 return t,states
def pieces(s):return [x for x in s.split(';') if x and not x.startswith('%(')]
def flags(meta,source):
 cpp=source.suffix.lower()!='.c'
 a=['/nologo','/c','/MT','/O2','/Ob2','/Oi','/Gy','/GF','/GS-','/GL','/W3','/utf-8','/D_UNICODE','/DUNICODE']
 if cpp:a+=['/std:c++17','/GR-','/Zc:char8_t-']
 if meta.get('FloatingPointModel')=='Fast':a+=['/fp:fast']
 if meta.get('ConformanceMode')=='true':a+=['/permissive-']
 if meta.get('TreatWarningAsError')=='true':a+=['/WX','/W4']
 a+=['/D'+s for s in pieces(meta.get('PreprocessorDefinitions',''))]
 a+=['/I'+s for s in pieces(meta.get('AdditionalIncludeDirectories',''))]
 a+=['/I'+str(p) for p in [VC/'include',VC/'atlmfc/include']+[SDK/'Include/10.0.26100.0'/n for n in ['ucrt','shared','um','winrt']]]
 a+=['/FI'+s for s in pieces(meta.get('ForcedIncludeFiles',''))]
 a+=['/wd'+s for s in pieces(meta.get('DisableSpecificWarnings',''))]
 extra=meta.get('AdditionalOptions','').replace('%(AdditionalOptions)','').strip()
 if extra:a+=extra.split()
 return a
projects=[S/'ReShade.vcxproj',S/'ReShadeFX.vcxproj']+[S/'deps'/n for n in ['fpng.vcxproj','glad.vcxproj','ImGui.vcxproj','jxl_simple_lossless.vcxproj','MinHook.vcxproj','stb.vcxproj']]
jobs=[];allobjs=[];coremeta=None
for project in projects:
 t,states=parse(project)
 if project.name=='ReShade.vcxproj':coremeta=states
 for node in t.findall('.//m:ClCompile[@Include]',NS):
  meta=dict(states.get('ClCompile',{}));merge_meta(meta,node)
  if meta.get('ExcludedFromBuild')=='true':continue
  src=(project.parent/node.get('Include')).resolve()
  if not src.is_file():raise RuntimeError('Missing source '+str(src))
  rel=src.relative_to(S);obj=OUT/'obj'/rel.with_suffix('.obj');obj.parent.mkdir(parents=True,exist_ok=True)
  args=[str(BIN/'cl.exe')]+flags(meta,src)+[str(src),'/Fo'+str(obj)]
  stamp=obj.with_suffix('.build.json');sig=hashlib.sha256(json.dumps(args).encode()+headerSignature+src.read_bytes()).hexdigest()
  allobjs.append(obj)
  if obj.exists() and stamp.exists() and json.loads(stamp.read_text()).get('signature')==sig:continue
  jobs.append((str(rel).replace('\\','_'),args,stamp,sig,obj))
status['sourcesTotal']=len(allobjs);status['compileNeeded']=len(jobs);save()
def compile_one(item):
 name,args,stamp,sig,obj=item
 rc=run('cc_'+name,args)
 if rc==0:stamp.write_text(json.dumps({'signature':sig,'objSha256':sha(obj)}))
 return rc
with ThreadPoolExecutor(max_workers=2) as pool:
 futures=[pool.submit(compile_one,j) for j in jobs]
 failed=False
 for f in as_completed(futures):
  try:rc=f.result()
  except Exception as ex:
   with lock:status['exception']=repr(ex);save()
   rc=1
  if rc:
   failed=True
   for x in futures:x.cancel()
   break
if failed:
 status['passed']=False;save();raise SystemExit(1)
res=OUT/'resource.res'
rcargs=[str(SDK/'bin/10.0.26100.0/x64/rc.exe'),'/nologo','/fo',str(res),'/d','RESHADE_LOCALIZATION','/d','NDEBUG','/d','_WIN64','/i',str(S/'res')]
for inc in [SDK/'Include/10.0.26100.0/um',SDK/'Include/10.0.26100.0/shared']:rcargs+=['/i',str(inc)]
rcargs+=[str(S/'res/resource.rc')]
if run('resource',rcargs,cwd=S/'res'):
 status['passed']=False;save();raise SystemExit(1)
libs=pieces(coremeta.get('Link',{}).get('AdditionalDependencies',''))
libs+=['kernel32.lib','user32.lib','gdi32.lib','winspool.lib','comdlg32.lib','advapi32.lib','shell32.lib','ole32.lib','oleaut32.lib','uuid.lib','odbc32.lib','odbccp32.lib','delayimp.lib']
dll=OUT/'ReShade64.dll'
args=['/nologo','/DLL','/MACHINE:X64','/SUBSYSTEM:WINDOWS','/INCREMENTAL:NO','/LTCG','/cgthreads:2','/OPT:REF','/OPT:NOICF','/DEF:'+str(S/'res/exports.def'),'/OUT:'+str(dll),'/IMPLIB:'+str(OUT/'ReShade64.lib')]
args+=['/LIBPATH:'+str(p) for p in [VC/'lib/x64',LIB/'um/x64',LIB/'ucrt/x64']]
args+=['/DELAYLOAD:'+s for s in pieces(coremeta.get('Link',{}).get('DelayLoadDLLs',''))]
args+=[str(p) for p in allobjs]+[str(res)]+libs
rsp=OUT/'link.rsp';rsp.write_text(subprocess.list2cmdline(args),encoding='utf-8')
rc=run('link',[str(BIN/'link.exe'),'@'+str(rsp)],timeout=300)
status['passed']=rc==0
if rc==0:status['dll']={'path':str(dll),'bytes':dll.stat().st_size,'sha256':sha(dll)}
status['ended']=datetime.datetime.now().astimezone().isoformat();status['freeRamAtEnd']=free_ram();save()
print(json.dumps({k:v for k,v in status.items() if k!='steps'},indent=2),flush=True)
