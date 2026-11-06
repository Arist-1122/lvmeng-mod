"""Build and offline-test the manager from the shipped source tree."""
from pathlib import Path
import hashlib,json,os,subprocess

SOURCE=Path(__file__).resolve().parent
ROOT=SOURCE.parent
OUT=ROOT/'manager';OUT.mkdir(exist_ok=True)
(ROOT/'build').mkdir(parents=True,exist_ok=True)
CSC=Path(os.environ.get('WINDIR',r'C:\Windows'))/'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
args=[str(CSC),'/nologo','/platform:x64','/target:winexe','/optimize+','/out:'+str(OUT/'LvmengModManager.exe'),'/r:System.Windows.Forms.dll','/r:System.Drawing.dll','/r:System.Web.Extensions.dll',str(SOURCE/'manager/ModManager.cs'),str(SOURCE/'manager/GameLocation.cs'),str(SOURCE/'manager/AssemblyInfo.cs'),str(SOURCE/'tests/ManagerSelfTests.cs'),str(SOURCE/'common/GameCompatibility.cs')]
p=subprocess.run(args,capture_output=True,text=True);(ROOT/'build/manager_compile.log').write_text(p.stdout+p.stderr,encoding='utf-8');assert p.returncode==0,p.stdout+p.stderr
test=subprocess.run([str(OUT/'LvmengModManager.exe'),'--self-test',str(ROOT/'validation/manager')],capture_output=True,timeout=30,creationflags=subprocess.CREATE_NO_WINDOW)
proof=dict(compiled=True,selfTestsPassed=test.returncode==0,sha256=hashlib.sha256((OUT/'LvmengModManager.exe').read_bytes()).hexdigest(),gameLaunched=False,loaderInvoked=False)
(ROOT/'validation/manager_build.json').write_text(json.dumps(proof,indent=2),encoding='utf-8');print(json.dumps(proof));raise SystemExit(test.returncode)
