"""Rebuild all packaged entrypoints from this source tree. Never starts a game.

Requires Windows x64, .NET Framework 4 CSC and the MSVC/Windows SDK toolchain.
Pass --toolchain or set LVMENG_NATIVE_TOOLCHAIN to the portable toolchain root.
Precompiled reviewed shader/BC7 payloads remain immutable inputs; this rebuilds
native dispatch, Bootstrap and managed launchers and refreshes their hash chain.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess

SOURCE=Path(__file__).resolve().parent
HASH=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
READ=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
WRITE=lambda p,x:p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

def build(runtime,output,toolchain):
    output.mkdir(parents=True,exist_ok=True);steps=[]
    vc=max((toolchain/'msvc/VC/Tools/MSVC').iterdir());sdk=toolchain/'sdk/microsoft.windows.sdk.cpp/c';lib=toolchain/'sdk/microsoft.windows.sdk.cpp.x64/c';version=max((sdk/'Include').iterdir()).name
    includes=[vc/'include',SOURCE/'vendor/reshade',*[sdk/'Include'/version/x for x in ('ucrt','shared','um','winrt')]]
    libraries=[vc/'lib/x64',lib/'um/x64',lib/'ucrt/x64'];binary=vc/'bin/Hostx64/x64';csc=Path(os.environ.get('WINDIR',r'C:\Windows'))/'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
    def run(label,args):
        process=subprocess.run([str(x)for x in args],cwd=output,capture_output=True,text=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        (output/(label+'.log')).write_text(process.stdout+process.stderr,encoding='utf-8');steps.append(dict(name=label,exitCode=process.returncode))
        if process.returncode:raise RuntimeError(label+'\n'+process.stdout+process.stderr)
        return process.stdout
    contract_sha=HASH(runtime/'runtime_contract.json');core_sha=HASH(runtime/'core/dxgi.dll')
    (SOURCE/'native/OutfitContract.hpp').write_text('#pragma once\n#include "PortableRuntime.hpp"\nconstexpr char runtime_contract_sha[]="'+contract_sha+'";\nconstexpr char required_core_sha[]="'+core_sha+'";\n',encoding='ascii')
    run('actor_compile',[binary/'cl.exe','/nologo','/c','/std:c++17','/MT','/O2','/W4','/EHsc','/utf-8','/D_WIN32_WINNT=0x0A00',*('/I'+str(x)for x in includes),SOURCE/'native/OutfitActor.cpp','/Fo'+str(output/'ComboActor.obj')])
    run('actor_link',[binary/'link.exe','/nologo','/DLL','/MACHINE:X64','/OUT:'+str(runtime/'addons/ComboActor.addon64'),'/IMPLIB:'+str(output/'ComboActor.lib'),output/'ComboActor.obj','kernel32.lib','bcrypt.lib',*('/LIBPATH:'+str(x)for x in libraries)])
    run('bootstrap_compile',[binary/'cl.exe','/nologo','/c','/O2','/GS-','/Zl','/W4','/utf-8',*('/I'+str(x)for x in includes),SOURCE/'bootstrap/PathPinBootstrap.c','/Fo'+str(output/'bootstrap.obj')])
    run('bootstrap_link',[binary/'link.exe','/nologo','/dll','/machine:x64','/entry:LibMain','/nodefaultlib','/out:'+str(runtime/'core/PathPinBootstrap.dll'),'/def:'+str(SOURCE/'bootstrap/PathPinBootstrap.def'),'/implib:'+str(output/'bootstrap.lib'),output/'bootstrap.obj',*('/libpath:'+str(x)for x in libraries),'kernel32.lib','user32.lib','libcmt.lib'])
    for filename,exe,kind in [('ModelTool.cs','LvmengModelTool.exe','winexe'),('LaunchHelper.cs','LaunchHelper.exe','winexe'),('NormalLibraryLoad.cs','NormalLibraryLoad.exe','exe')]:
        args=[csc,'/nologo','/platform:x64','/optimize+','/target:'+kind,'/out:'+str(runtime/exe),'/r:System.Web.Extensions.dll']
        if filename=='ModelTool.cs':args+=['/r:System.Windows.Forms.dll','/r:System.Drawing.dll']
        sources=[SOURCE/'managed'/filename,SOURCE/'managed/GameLocation.cs',SOURCE/'common/GameCompatibility.cs']
        if filename=='ModelTool.cs':sources.append(SOURCE/'common/LaunchPrivileges.cs')
        run('managed_'+Path(filename).stem,args+sources)
    # Execute the unchanged production inl with an API shim; no GPU/core/game.
    run('surface_probe_compile',[binary/'cl.exe','/nologo','/std:c++17','/MT','/O2','/W4','/EHsc','/utf-8',*('/I'+str(x)for x in includes),SOURCE/'tests/SurfaceScopeProbe.cpp','/Fe'+str(output/'SurfaceScopeProbe.exe'),'/Fo'+str(output/'SurfaceScopeProbe.obj'),'/link',*('/LIBPATH:'+str(x)for x in libraries),'kernel32.lib'])
    dispatch=json.loads(run('surface_probe_run',[output/'SurfaceScopeProbe.exe']));assert dispatch['passed'] and dispatch['programs']==21
    dispatch['productionInlSha256']=HASH(SOURCE/'native/SurfaceLightRuntime.inl');WRITE(output/'production_dispatch.json',dispatch)
    from test_launch_privileges import check as check_launch_privileges
    launch_checks=check_launch_privileges(output/'launch-tests')
    (runtime/'research/bootstrap_sha256.txt').write_text(HASH(runtime/'core/PathPinBootstrap.dll')+'\n',encoding='ascii')
    package=READ(runtime/'package.json');package['contractSha256']=contract_sha
    names={x['path']for x in package['files']}
    for file in (runtime/'payload').rglob('*'):
        if file.is_file():
            name=file.relative_to(runtime).as_posix()
            if name not in names:package['files'].append(dict(path=name,sha256=''));names.add(name)
    for row in package['files']:row['sha256']=HASH(runtime/row['path'])
    WRITE(runtime/'package.json',package)
    sources={p.relative_to(SOURCE).as_posix():HASH(p)for p in SOURCE.rglob('*')if p.is_file()and '__pycache__'not in p.parts and not p.resolve().is_relative_to(output.resolve())}
    report=dict(passed=True,steps=steps,programs=21,files=len(package['files']),contractSha256=contract_sha,addonSha256=HASH(runtime/'addons/ComboActor.addon64'),sourceFiles=sources,launchPolicyTestsPassed=launch_checks['passed'],actualElevatedBridgeTested=False,gameLaunched=False,loaderInvoked=False,adminRequested=False)
    WRITE(output/'build_result.json',report);return report

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runtime',type=Path,default=SOURCE.parent/'runtime');p.add_argument('--output',type=Path,default=SOURCE.parent/'build');p.add_argument('--toolchain',type=Path,default=Path(os.environ['LVMENG_NATIVE_TOOLCHAIN']) if os.environ.get('LVMENG_NATIVE_TOOLCHAIN') else None);a=p.parse_args()
    if a.toolchain is None:p.error('Pass --toolchain or set LVMENG_NATIVE_TOOLCHAIN')
    result=build(a.runtime.resolve(),a.output.resolve(),a.toolchain.resolve());print(json.dumps({k:v for k,v in result.items()if k!='sourceFiles'}))
