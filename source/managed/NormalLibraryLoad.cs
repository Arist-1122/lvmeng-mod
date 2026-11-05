using System;
using System.IO;
using System.Text;
using System.Linq;
using System.Diagnostics;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Web.Script.Serialization;
using System.Threading;

// Ordinary Windows LoadLibraryW only. No executable trampoline, privilege
// adjustment, protection changes, driver operations, thread hijack, or retries.
class NormalLibraryLoad {
 [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,int pid);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool CloseHandle(IntPtr h);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr h,out long created,out long exited,out long kernel,out long user);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool IsWow64Process(IntPtr p,out bool wow64);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern IntPtr GetModuleHandle(string name);
 [DllImport("kernel32.dll",CharSet=CharSet.Ansi,ExactSpelling=true)] static extern IntPtr GetProcAddress(IntPtr module,string name);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr LoadLibrary(string path);
 [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr module);
 [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr VirtualAllocEx(IntPtr p,IntPtr address,UIntPtr size,uint allocation,uint protect);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool VirtualFreeEx(IntPtr p,IntPtr address,UIntPtr size,uint freeType);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool WriteProcessMemory(IntPtr p,IntPtr address,byte[] data,UIntPtr size,out UIntPtr written);
 [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr CreateRemoteThread(IntPtr p,IntPtr attrs,UIntPtr stack,IntPtr start,IntPtr arg,uint flags,out uint tid);
 [DllImport("kernel32.dll",SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle,uint ms);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetExitCodeThread(IntPtr thread,out uint code);
 static Dictionary<string,object> result=new Dictionary<string,object>();
 static List<object> events=new List<object>();
 static void Event(string name,object detail){events.Add(new{at=DateTimeOffset.Now.ToString("o"),name=name,detail=detail});}
 static Exception Error(string stage){return new Exception(stage+": Win32 "+Marshal.GetLastWin32Error());}
 static string Digest(string path){using(var h=SHA256.Create())using(var s=File.OpenRead(path))return BitConverter.ToString(h.ComputeHash(s)).Replace("-","").ToLowerInvariant();}
 static ProcessModule[] Modules(Process p){p.Refresh();return p.Modules.Cast<ProcessModule>().ToArray();}
 static bool Same(string a,string b){return string.Equals(Path.GetFullPath(a),Path.GetFullPath(b),StringComparison.OrdinalIgnoreCase);}
 static int Main(string[] args){
  string root=AppDomain.CurrentDomain.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
  string dll=Path.Combine(root,"core","dxgi.dll");
  string bootstrap=Path.Combine(root,"core","PathPinBootstrap.dll");
  string output=Path.Combine(root,"logs","normal_load_"+DateTime.Now.ToString("yyyyMMddTHHmmssfff")+"_"+Process.GetCurrentProcess().Id+".json");
  IntPtr process=IntPtr.Zero,memory=IntPtr.Zero,thread=IntPtr.Zero,localBootstrap=IntPtr.Zero;Process target=null;
  bool threadStarted=false,threadFinished=false;int exit=2;
  Mutex loadLock=null;bool ownsLock=false;
  result["at"]=DateTimeOffset.Now.ToString("o");result["completed"]=false;result["renderHookVerified"]=false;
  result["dll"]=dll;result["events"]=events;result["output"]=output;
  result["operation"]="One ordinary LoadLibraryW bootstrap, then its exported function pins ReShade base path during core load and restores the previous value; no retry";
  try {
   loadLock=new Mutex(false,@"Global\LvmengModelTrial_OrdinaryLoad_v1");try{ownsLock=loadLock.WaitOne(0);}catch(AbandonedMutexException){ownsLock=true;}if(!ownsLock)throw new Exception("ANOTHER_ORDINARY_LOAD_ACTIVE");
   if(!Environment.Is64BitProcess)throw new Exception("Loader must be x64");
   if(args.Length!=2)throw new Exception("Usage: NormalLibraryLoad.exe <target PID> <expected UTC start ticks>");
   long expectedTicks=Int64.Parse(args[1]);
   int pid=int.Parse(args[0]);target=Process.GetProcessById(pid);
   if(target.StartTime.ToUniversalTime().Ticks!=expectedTicks)throw new Exception("TARGET_START_TIME_MISMATCH");
   if(target.ProcessName!="Lumin" && target.ProcessName!="LVMeng_DX12_POC")throw new Exception("TARGET_NAME_NOT_ALLOWED");
   result["targetPid"]=pid;result["targetName"]=target.ProcessName;result["targetStartTime"]=target.StartTime.ToUniversalTime().ToString("o");
   if(!File.Exists(Path.Combine(root,"package.json")))throw new Exception("RUNTIME_PACKAGE_MISSING");
   if(!File.Exists(Path.Combine(root,"core","ReShade.ini")))throw new Exception("EXTERNAL_CONFIG_MISSING");
   string sha=Digest(dll);result["dllSha256"]=sha;
   if(sha!="1c82a60c70735dc3f1938a4761d29e0012897acd72f6f054d2a53d666b9c6665")throw new Exception("DLL_HASH_MISMATCH");
   string bootSha=Digest(bootstrap);result["bootstrapSha256"]=bootSha;
   if(bootSha!=File.ReadAllText(Path.Combine(root,"research","bootstrap_sha256.txt")).Trim())throw new Exception("BOOTSTRAP_HASH_MISMATCH");
   process=OpenProcess(0x43a,false,pid);if(process==IntPtr.Zero)throw Error("OPEN_PROCESS_DENIED_STOP");
   long created,exited,kernel,user;if(!GetProcessTimes(process,out created,out exited,out kernel,out user)||DateTime.FromFileTimeUtc(created).Ticks!=expectedTicks)throw new Exception("OPENED_HANDLE_START_TIME_MISMATCH");
   Event("process_opened",new{access="0x43a",privilegesAdjusted=false,startTimeRevalidatedOnHandle=true});
   bool wow64;if(!IsWow64Process(process,out wow64))throw Error("IS_WOW64_QUERY_FAILED");if(wow64)throw new Exception("TARGET_NOT_X64");
   var before=Modules(target);
   if(before.Any(m=>Same(m.FileName,dll)||Same(m.FileName,bootstrap)))throw new Exception("ALREADY_LOADED_NO_RETRY");
   string targetExecutable=before.Single(m=>m.ModuleName.Equals(target.ProcessName+".exe",StringComparison.OrdinalIgnoreCase)).FileName;
   if(target.ProcessName=="Lumin" && !Same(targetExecutable,GameLocation.Read(root).gameExecutable))throw new Exception("GAME_EXECUTABLE_PATH_NOT_ALLOWED");
   if(target.ProcessName=="LVMeng_DX12_POC" && !Same(targetExecutable,Path.Combine(root,"harness","LVMeng_DX12_POC.exe")))throw new Exception("POC_EXECUTABLE_PATH_NOT_ALLOWED");
   string targetDirectory=Path.GetDirectoryName(targetExecutable);
   if(File.Exists(Path.Combine(targetDirectory,"ReShade.ini")))throw new Exception("TARGET_BASEPATH_CONFIG_EXISTS_REVIEW_REQUIRED");
   // Resolve the owning local module and use its RVA in the verified remote
   // System32 module, rather than assuming identical virtual addresses.
   IntPtr localFunction=GetProcAddress(GetModuleHandle("kernel32.dll"),"LoadLibraryW");if(localFunction==IntPtr.Zero)throw Error("LOCAL_EXPORT_FAILED");
   long address=localFunction.ToInt64();
   var owner=Modules(Process.GetCurrentProcess()).Single(m=>address>=m.BaseAddress.ToInt64() && address<m.BaseAddress.ToInt64()+m.ModuleMemorySize);
   var remoteOwner=before.Single(m=>m.ModuleName.Equals(owner.ModuleName,StringComparison.OrdinalIgnoreCase));
   if(!Same(owner.FileName,remoteOwner.FileName) || !Path.GetFullPath(owner.FileName).StartsWith(Environment.GetFolderPath(Environment.SpecialFolder.System)+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))throw new Exception("SYSTEM_MODULE_IDENTITY_MISMATCH");
   IntPtr remoteFunction=new IntPtr(remoteOwner.BaseAddress.ToInt64()+address-owner.BaseAddress.ToInt64());
   byte[] argument=Encoding.Unicode.GetBytes(bootstrap+"\0");
   memory=VirtualAllocEx(process,IntPtr.Zero,(UIntPtr)argument.Length,0x3000,0x04);if(memory==IntPtr.Zero)throw Error("RW_ARGUMENT_ALLOCATION_FAILED_STOP");
   UIntPtr written;if(!WriteProcessMemory(process,memory,argument,(UIntPtr)argument.Length,out written)||written.ToUInt64()!=(ulong)argument.Length)throw Error("ARGUMENT_WRITE_FAILED_STOP");
   Event("path_argument_written",new{bytes=argument.Length,pageProtection="READWRITE",executableMemoryAllocated=false});
   uint tid;thread=CreateRemoteThread(process,IntPtr.Zero,UIntPtr.Zero,remoteFunction,memory,0,out tid);if(thread==IntPtr.Zero)throw Error("NORMAL_THREAD_CREATE_DENIED_STOP");
   threadStarted=true;Event("load_thread_started",new{threadId=tid,systemFunction="LoadLibraryW",owner=owner.ModuleName});
   uint wait=WaitForSingleObject(thread,10000);
   if(wait!=0)throw new Exception("LOAD_WAIT_NOT_COMPLETED_"+wait+"_NO_RETRY_NO_FORCED_THREAD_TERMINATION");
   threadFinished=true;uint code;if(!GetExitCodeThread(thread,out code))throw Error("THREAD_EXIT_QUERY_FAILED");result["bootstrapLoadThreadExitCode32"]=code;
   var remoteBootstrap=Modules(target).SingleOrDefault(m=>Same(m.FileName,bootstrap));
   if(remoteBootstrap==null)throw new Exception("BOOTSTRAP_NOT_PRESENT_AFTER_NORMAL_LOAD");
   result["bootstrapModuleLoaded"]=true;
   localBootstrap=LoadLibrary(bootstrap);if(localBootstrap==IntPtr.Zero)throw Error("LOCAL_BOOTSTRAP_EXPORT_MAP_FAILED");
   var startExport=GetProcAddress(localBootstrap,"StartReShade");if(startExport==IntPtr.Zero)throw Error("BOOTSTRAP_EXPORT_MISSING");
   var remoteStart=new IntPtr(remoteBootstrap.BaseAddress.ToInt64()+startExport.ToInt64()-localBootstrap.ToInt64());
   CloseHandle(thread);thread=IntPtr.Zero;
   if(!VirtualFreeEx(process,memory,UIntPtr.Zero,0x8000))throw Error("ARGUMENT_FREE_FAILED");memory=IntPtr.Zero;result["argumentMemoryFreed"]=true;
   threadFinished=false;
   thread=CreateRemoteThread(process,IntPtr.Zero,UIntPtr.Zero,remoteStart,IntPtr.Zero,0,out tid);if(thread==IntPtr.Zero)throw Error("BOOTSTRAP_EXPORT_THREAD_DENIED_STOP");
   Event("path_pinned_core_load_started",new{threadId=tid,export="StartReShade",executableMemoryAllocated=false});
   wait=WaitForSingleObject(thread,10000);if(wait!=0){result["coreLoadMayStillBeRunning"]=true;result["environmentRestoreUnconfirmed"]=true;throw new Exception("CORE_LOAD_WAIT_NOT_COMPLETED_"+wait+"_NO_RETRY");}
   threadFinished=true;if(!GetExitCodeThread(thread,out code))throw Error("CORE_LOAD_EXIT_QUERY_FAILED");result["coreLoadWin32ExitCode"]=code;
   if(code!=0)throw new Exception("CORE_LOAD_OR_ENV_RESTORE_FAILED_WIN32_"+code);
   var loaded=Modules(target).Where(m=>Same(m.FileName,dll)).Select(m=>new{m.ModuleName,m.FileName,m.ModuleMemorySize}).ToArray();
   result["loadedModule"]=loaded;
   if(loaded.Length!=1)throw new Exception("DLL_NOT_PRESENT_AFTER_NORMAL_LOAD; no target GetLastError was queried");
   result["completed"]=true;result["status"]="dll_loaded_only_render_hook_not_yet_verified";exit=0;
  }catch(Exception e){result["error"]=e.Message;result["status"]="stopped_without_bypass";Console.Error.WriteLine(e.Message);}
  finally{
   if(memory!=IntPtr.Zero && (!threadStarted || threadFinished))result["argumentMemoryFreed"]=VirtualFreeEx(process,memory,UIntPtr.Zero,0x8000);
   else if(memory!=IntPtr.Zero)result["argumentMemoryRetainedForPendingThread"]=true;
   if(thread!=IntPtr.Zero)CloseHandle(thread);if(process!=IntPtr.Zero)CloseHandle(process);if(localBootstrap!=IntPtr.Zero)FreeLibrary(localBootstrap);if(target!=null)target.Dispose();if(ownsLock)loadLock.ReleaseMutex();if(loadLock!=null)loadLock.Dispose();
   result["finishedAt"]=DateTimeOffset.Now.ToString("o");result["privilegesAdjusted"]=false;result["protectionChanged"]=false;result["gameInstallationWrittenByLoader"]=false;
   Directory.CreateDirectory(Path.GetDirectoryName(output));File.WriteAllText(output,new JavaScriptSerializer().Serialize(result));Console.WriteLine(output);
  }
  return exit;
 }
}
