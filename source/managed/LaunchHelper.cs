using System;
using System.IO;
using System.Text;
using System.Diagnostics;
using System.Threading;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Web.Script.Serialization;
using System.Collections.Generic;

class LaunchHelper {
    class Item {public string path,sha256;}
    class Package {public int version;public bool runtimeReady;public string helperPath,loaderPath,corePath,bootstrapPath,addonPath,contractPath,configTemplatePath,contractSha256;public Item[] files,gameSamples;}
    static readonly string Root=AppDomain.CurrentDomain.BaseDirectory;
    static readonly JavaScriptSerializer Json=new JavaScriptSerializer();
    static readonly UTF8Encoding Utf8=new UTF8Encoding(false);
    static Dictionary<string,object> state=new Dictionary<string,object>();
    static string status,session;static int guiPid;static long guiStartTicks;
    static void CheckOwner(){
        if(File.Exists(Path.Combine(Root,"control","cancel_"+session+".json")))throw new Exception("启动窗口已取消本次会话。");
        using(var gui=Process.GetProcessById(guiPid))if(gui.HasExited||gui.StartTime.ToUniversalTime().Ticks!=guiStartTicks||!Path.GetFullPath(gui.MainModule.FileName).Equals(At("LvmengModelTool.exe"),StringComparison.OrdinalIgnoreCase))throw new Exception("原启动窗口已关闭，停止本次监听。");
    }
    static string Hash(string p){using(var h=SHA256.Create())using(var f=File.OpenRead(p))return BitConverter.ToString(h.ComputeHash(f)).Replace("-","").ToLowerInvariant();}
    static string At(string relative){if(String.IsNullOrWhiteSpace(relative)||Path.IsPathRooted(relative))throw new Exception("包内路径无效。");string p=Path.GetFullPath(Path.Combine(Root,relative));if(!p.StartsWith(Path.GetFullPath(Root).TrimEnd('\\')+"\\",StringComparison.OrdinalIgnoreCase))throw new Exception("包内路径越界。");return p;}
    static void Save(string value){state["state"]=value;state["atUtc"]=DateTime.UtcNow.ToString("o");Directory.CreateDirectory(Path.GetDirectoryName(status));string tmp=status+".tmp";File.WriteAllText(tmp,Json.Serialize(state),Utf8);if(File.Exists(status))File.Replace(tmp,status,null);else File.Move(tmp,status);}
    static void SaveIdentity(Package package,int targetPid,DateTime created){
        string path=At("control/session_"+targetPid+".json"),temp=path+".tmp";
        var identity=new{version=1,sessionId=session,contractSha256=package.contractSha256,targetPid=targetPid,targetStartUtc=created.ToString("o"),ownerGuiPid=guiPid,ownerGuiStartTicks=guiStartTicks};
        File.WriteAllText(temp,Json.Serialize(identity),Utf8);if(File.Exists(path))File.Replace(temp,path,null);else File.Move(temp,path);
    }
    static Package ValidatePackage(){
        string file=Path.Combine(Root,"package.json");if(new FileInfo(file).Length>262144)throw new Exception("发布清单过大。");
        var p=Json.Deserialize<Package>(File.ReadAllText(file,Utf8));
        if(p.version!=1||!p.runtimeReady||p.files==null||p.files.Length<1||p.files.Length>256||p.gameSamples==null||p.gameSamples.Length==0)throw new Exception("替换构建尚未通过运行检查。");
        var verified=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
        foreach(var f in p.files){string path=At(f.path);if(verified.ContainsKey(path))throw new Exception("文件清单包含重复路径。");if(Hash(path)!=f.sha256)throw new Exception("包内文件发生变化："+f.path);verified.Add(path,f.sha256);}
        foreach(var name in new[]{"LvmengModelTool.exe",p.helperPath,p.loaderPath,p.corePath,p.bootstrapPath,p.addonPath,p.contractPath,p.configTemplatePath})if(!verified.ContainsKey(At(name)))throw new Exception("缺少必要运行文件校验："+name);
        if(verified[At(p.contractPath)]!=p.contractSha256)throw new Exception("合同校验关联不一致。");
        foreach(var f in p.gameSamples){string path=GameLocation.Read(Root).Sample(f.path);if(Hash(path)!=f.sha256)throw new Exception("客户端版本校验失败。");}
        string addon=At(p.addonPath);foreach(var candidate in Directory.GetFiles(Path.GetDirectoryName(addon),"*",SearchOption.AllDirectories)){
            string ext=Path.GetExtension(candidate).ToLowerInvariant();if((ext==".dll"||ext==".addon"||ext==".addon32"||ext==".addon64")&&!candidate.Equals(addon,StringComparison.OrdinalIgnoreCase))throw new Exception("发现未校验的额外图形组件，请保留本程序独立目录。");
        }
        At(p.loaderPath);if(p.contractSha256==null||p.contractSha256.Length!=64)throw new Exception("运行合同缺失。");return p;
    }
    static int Main(string[] args){
        session=args.Length==4?args[1]:"";Guid g;
        if(args.Length!=4||args[0]!="--arm"||session.Length!=32||!Guid.TryParseExact(session,"N",out g)||!Int32.TryParse(args[2],out guiPid)||guiPid<=0||!Int64.TryParse(args[3],out guiStartTicks)||guiStartTicks<=0)return 2;
        status=Path.Combine(Root,"control","launch_"+session+".json");
        state["version"]=1;state["sessionId"]=session;state["automaticRetry"]=false;state["targetPid"]=0;
        Mutex launchLock=null;bool ownsLock=false;
        try{
            launchLock=new Mutex(false,@"Global\LvmengModelTrial_LuminLoad_v1");
            try{ownsLock=launchLock.WaitOne(0);}catch(AbandonedMutexException){ownsLock=true;}
            if(!ownsLock)throw new Exception("另一个启动助手正在处理本次游戏启动。");
            if(!new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator))throw new Exception("启动助手未取得本次所需权限。");
            CheckOwner();var package=ValidatePackage();state["contractSha256"]=package.contractSha256;state["guiPid"]=guiPid;state["guiStartTicks"]=guiStartTicks;
            var before=Process.GetProcessesByName("Lumin");try{if(before.Length!=0)throw new Exception("游戏仍在运行，请先正常退出。");}finally{foreach(var p in before)p.Dispose();}
            // ReShade may save UI preferences into its working INI. The pinned
            // template is immutable; only our separate working profile resets.
            string profile=At("core/ReShade.ini");if(File.Exists(profile))File.Copy(profile,At("logs/profile_before_"+session+".ini"),false);
            File.Copy(At(package.configTemplatePath),profile,true);state["profileSha256"]=Hash(profile);
            Process.GetCurrentProcess().PriorityClass=ProcessPriorityClass.BelowNormal;
            DateTime armedAt=DateTime.UtcNow,deadline=armedAt.AddSeconds(180);state["armedAtUtc"]=armedAt.ToString("o");Save("armed");
            Process target=null;DateTime lastOwnerCheck=DateTime.MinValue;
            while(DateTime.UtcNow<deadline){
                if((DateTime.UtcNow-lastOwnerCheck).TotalMilliseconds>=200){CheckOwner();lastOwnerCheck=DateTime.UtcNow;}
                var candidates=Process.GetProcessesByName("Lumin");
                if(candidates.Length>1){foreach(var p in candidates)p.Dispose();throw new Exception("发现多个游戏进程，停止本次加载。");}
                if(candidates.Length==1){target=candidates[0];break;}Thread.Sleep(20);
            }
            if(target==null)throw new Exception("3分钟内未收到新的游戏进程。没有执行加载。");
            using(target){
                DateTime created=target.StartTime.ToUniversalTime();
                if(created<armedAt)throw new Exception("游戏进程早于本次启动监听。");
                string exe=Path.GetFullPath(target.MainModule.FileName);
                if(!exe.Equals(GameLocation.Read(Root).gameExecutable,StringComparison.OrdinalIgnoreCase))throw new Exception("游戏路径不匹配。");
                state["targetPid"]=target.Id;state["targetStartUtc"]=created.ToString("o");CheckOwner();SaveIdentity(package,target.Id,created);Save("loading");
                CheckOwner();var pi=new ProcessStartInfo(At(package.loaderPath),target.Id+" "+created.Ticks){UseShellExecute=false,CreateNoWindow=true,WindowStyle=ProcessWindowStyle.Hidden,WorkingDirectory=Root};
                using(var loader=Process.Start(pi)){
                    if(!loader.WaitForExit(30000)){state["loaderMayStillBeRunning"]=true;throw new Exception("加载器尚未返回；停止等待，不再尝试加载。");}
                    state["loaderExitCode"]=loader.ExitCode;if(loader.ExitCode!=0)throw new Exception("本次图形加载未完成，请查看加载记录。");
                }
                target.Refresh();if(target.HasExited||target.StartTime.ToUniversalTime()!=created)throw new Exception("目标游戏会话已结束。");
                Save("loaded");return 0;
            }
        }catch(Exception e){state["error"]=e.Message;state["exitCode"]=2;try{Save("failed");}catch{}return 2;}
        finally{if(ownsLock)launchLock.ReleaseMutex();if(launchLock!=null)launchLock.Dispose();}
    }
}
