using System;
using System.IO;
using System.Text;
using System.Linq;
using System.Drawing;
using System.Diagnostics;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using System.Runtime.InteropServices;

// The GUI is always unelevated. Only its explicitly clicked launch helper asks
// Windows for elevation; the TapTap URI is opened by this original GUI process.
// Restoring the model changes a bounded command, never unloads an in-flight DLL.
class ModelTool : Form {
    public class FileItem { public string path,sha256; }
    public class Package {
        public int version; public bool runtimeReady,liveGameVerified;
        public string contractSha256,releaseLabel,helperPath,loaderPath,corePath,bootstrapPath,addonPath,contractPath,configTemplatePath,previewPath,reportPath;
        public FileItem[] files,gameSamples;
    }
    public class HelperState { public string sessionId,state,error,targetStartUtc; public int targetPid,exitCode; }
    public class AddonState {
        public int version,pid; public long commandId,replacedDraws;
        public string sessionId,targetStartUtc,contractSha256,mode,rejectReason;
        public uint missingMask; public bool trackingFailed;
        public long outfitBody01Mips,outfitBody02Mips,hairSilverMips,hairLightDraws,gloveLightDraws,surfaceLightDraws,weaponMips,groundGuardSubmissions,primaryPreviewSubmissions; public bool primaryPreviewActive,primaryPreviewWaiting; public bool outfitAlreadyUploaded,outfitUploadReady;
    }
    static readonly string Root=AppDomain.CurrentDomain.BaseDirectory;
    static readonly JavaScriptSerializer Json=new JavaScriptSerializer();
    static readonly UTF8Encoding Utf8=new UTF8Encoding(false);
    static readonly string Control=Path.Combine(Root,"control"),Logs=Path.Combine(Root,"logs");
    [DllImport("kernel32.dll")] static extern ulong GetTickCount64();
    Package package;
    Label headline,description,detail;
    Button start,restore,check,preview,report;
    TextBox events;
    System.Windows.Forms.Timer timer=new System.Windows.Forms.Timer();
    string sessionId,helperStatePath,lastState,lastRejectReason; int targetPid; string targetStartUtc;
    CheckBox sharedEffects;
    long commandId; bool wantReplacement,uriOpened,helperPending,closing;
    DateTime lastRenew=DateTime.MinValue,lastCommandAt=DateTime.MinValue; string logFile;
    static string SafePath(string relative) {
        if(String.IsNullOrWhiteSpace(relative)||Path.IsPathRooted(relative))throw new Exception("包内路径无效。");
        string p=Path.GetFullPath(Path.Combine(Root,relative));
        if(!p.StartsWith(Path.GetFullPath(Root).TrimEnd('\\')+"\\",StringComparison.OrdinalIgnoreCase))throw new Exception("包内路径越界。");
        return p;
    }
    static T Read<T>(string path) {
        // Status writers atomically replace the path. Allow that replacement
        // while this reader keeps its already-open, complete file instance.
        using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete)) {
            if(file.Length>262144)throw new Exception("状态文件过大。");
            using(var reader=new StreamReader(file,Utf8))return Json.Deserialize<T>(reader.ReadToEnd());
        }
    }
    static T ReadState<T>(string path) where T:class {
        try{return Read<T>(path);}
        catch(IOException e) {
            // An atomic rename can briefly prevent a NEW reader from opening
            // the path. Skip this timer observation; the next tick reads it.
            int code=e.HResult&65535;
            if(code==2||code==3||code==32||code==33)return null;
            throw;
        }
    }
    static string Hash(string p) {using(var h=SHA256.Create())using(var f=File.OpenRead(p))return BitConverter.ToString(h.ComputeHash(f)).Replace("-","").ToLowerInvariant();}
    static void Atomic(string p,object value) {
        Directory.CreateDirectory(Path.GetDirectoryName(p));string temp=p+"."+Guid.NewGuid().ToString("N")+".tmp";
        try {File.WriteAllText(temp,Json.Serialize(value),Utf8);if(File.Exists(p))File.Replace(temp,p,null);else File.Move(temp,p);}finally {if(File.Exists(temp))File.Delete(temp);}
    }
    static Package ValidatePackage(bool requireRuntime) {
        var p=Read<Package>(Path.Combine(Root,"package.json"));
        if(p.version!=1||p.files==null||p.files.Length<1||p.files.Length>256)throw new Exception("发布清单无效。");
        var verified=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
        foreach(var f in p.files){string path=SafePath(f.path);if(verified.ContainsKey(path))throw new Exception("文件清单包含重复路径。");if(Hash(path)!=f.sha256)throw new Exception("文件校验未通过："+f.path);verified.Add(path,f.sha256);}
        foreach(var name in new[]{"LvmengModelTool.exe",p.helperPath})if(!verified.ContainsKey(SafePath(name)))throw new Exception("缺少入口文件校验："+name);
        if(p.gameSamples==null)throw new Exception("缺少客户端版本检查。");
        foreach(var f in p.gameSamples) {
            string path=GameLocation.Read(Root).Sample(f.path);
            if(Hash(path)!=f.sha256)throw new Exception("客户端版本发生变化，需要重新检查适配。");
        }
        if(requireRuntime) {
            if(!p.runtimeReady)throw new Exception("此构建仍在验证中，尚未开放游戏替换。");
            if(p.contractSha256==null||p.contractSha256.Length!=64||p.gameSamples.Length==0)throw new Exception("运行合同不完整。");
            foreach(var name in new[]{p.loaderPath,p.corePath,p.bootstrapPath,p.addonPath,p.contractPath,p.configTemplatePath})if(!verified.ContainsKey(SafePath(name)))throw new Exception("缺少运行文件校验："+name);
            if(verified[SafePath(p.contractPath)]!=p.contractSha256)throw new Exception("合同校验关联不一致。");
        }
        return p;
    }
    ModelTool() {
        Text="莉特莉丝 · 红黑收尾修正版";ClientSize=new Size(790,625);MinimumSize=new Size(806,664);StartPosition=FormStartPosition.CenterScreen;
        Font=new Font("Microsoft YaHei UI",10);BackColor=Color.FromArgb(245,247,249);ForeColor=Color.FromArgb(31,43,54);
        var title=new Label{Text="莉特莉丝 · 红黑收尾修正版",Font=new Font(Font.FontFamily,21,FontStyle.Bold),AutoSize=true,Location=new Point(30,24)};Controls.Add(title);
        var subtitle=new Label{Text="红白黑原版服装 · 黑红无人机 · 全套战斗特效试用",AutoSize=true,Location=new Point(32,70),ForeColor=Color.FromArgb(90,106,119)};Controls.Add(subtitle);
        var card=new Panel{Location=new Point(30,112),Size=new Size(730,150),BackColor=Color.White,Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right};Controls.Add(card);
        headline=new Label{Location=new Point(20,15),Size=new Size(690,30),Font=new Font(Font.FontFamily,14,FontStyle.Bold)};card.Controls.Add(headline);
        description=new Label{Location=new Point(20,54),Size=new Size(690,50)};card.Controls.Add(description);
        detail=new Label{Location=new Point(20,113),Size=new Size(690,28),ForeColor=Color.FromArgb(90,106,119)};card.Controls.Add(detail);
        start=ButtonAt("启动莉特莉丝试用",30,282,280);start.BackColor=Color.FromArgb(37,106,91);start.ForeColor=Color.White;start.FlatStyle=FlatStyle.Flat;start.Click+=(s,e)=>StartTrial();
        restore=ButtonAt("停止后续换色",326,282,198);restore.Click+=(s,e)=>Restore();
        check=ButtonAt("检查文件",540,282,220);check.Click+=(s,e)=>CheckFiles();
        preview=ButtonAt("查看模型与对比",30,342,280);preview.Click+=(s,e)=>OpenLocal(package==null?null:package.previewPath);
        report=ButtonAt("查看验证记录",326,342,198);report.Click+=(s,e)=>OpenLocal(package==null?null:package.reportPath);
        var folder=ButtonAt("打开文件夹",540,342,220);folder.Click+=(s,e)=>Process.Start(new ProcessStartInfo(Root){UseShellExecute=true});
        sharedEffects=new CheckBox{Text="单人：自动启用全部已接入特效",Checked=true,AutoSize=true,Location=new Point(30,409)};
        sharedEffects.CheckedChanged+=(s,e)=>SetSharedEffects();
        Controls.Add(sharedEffects);
        events=new TextBox{Location=new Point(30,456),Size=new Size(730,132),Multiline=true,ReadOnly=true,ScrollBars=ScrollBars.Vertical,BackColor=Color.White,BorderStyle=BorderStyle.FixedSingle,Anchor=AnchorStyles.Top|AnchorStyles.Bottom|AnchorStyles.Left|AnchorStyles.Right};Controls.Add(events);
        Directory.CreateDirectory(Control);Directory.CreateDirectory(Logs);logFile=Path.Combine(Logs,"ui_"+DateTime.Now.ToString("yyyyMMddTHHmmssfff")+".txt");
        timer.Interval=1000;timer.Tick+=(s,e)=>Tick();timer.Start();Shown+=(s,e)=>CheckFiles();FormClosing+=(s,e)=>{closing=true;wantReplacement=false;timer.Stop();CancelSession();try{if(targetPid!=0)WriteCommand("original");}catch{/* No renewal continues. The native monotonic lease remains the fallback. */}};
    }
    Button ButtonAt(string text,int x,int y,int width) {var b=new Button{Text=text,Location=new Point(x,y),Size=new Size(width,44),FlatStyle=FlatStyle.Flat,BackColor=Color.White};Controls.Add(b);return b;}
    void Note(string text) {string line=DateTime.Now.ToString("HH:mm:ss")+"  "+text;events.AppendText(line+Environment.NewLine);File.AppendAllText(logFile,line+Environment.NewLine,Utf8);}
    void CancelSession(){try{if(!String.IsNullOrEmpty(sessionId))Atomic(Path.Combine(Control,"cancel_"+sessionId+".json"),new{sessionId=sessionId,atUtc=DateTime.UtcNow.ToString("o")});}catch{/* Owner liveness and the finite helper timeout remain in force. */}}
    void Status(string title,string text,string extra) {headline.Text=title;description.Text=text;detail.Text=extra;}
    void CheckFiles() {
        start.Enabled=false;
        try {package=ValidatePackage(false);start.Enabled=package.runtimeReady&&!helperPending;
            Status(package.runtimeReady?"已准备好试用":"构建验证中",package.runtimeReady?"请先正常退出游戏，再点击启动。完成 Windows 权限确认后，将按管理器设置启动；手动模式请自行打开游戏。":"替换组件还在完成验证。程序会显示真实进度，验证通过后才开放启动。",package.liveGameVerified?"已完成实机验证":"保留原版服装网格 · 退出游戏后恢复");
            start.BackColor=package.runtimeReady?Color.FromArgb(37,106,91):Color.FromArgb(224,230,234);
            Note("包内文件与客户端样本检查通过。"+(package.runtimeReady?"":"游戏入口暂未开放。"));
        }catch(Exception e){Status("检查未通过",e.Message,"停止启动，已有游戏不受影响。");Note(e.Message);}
    }
    void StartTrial() {
        try {
            package=ValidatePackage(true);
            if(new System.Security.Principal.WindowsPrincipal(System.Security.Principal.WindowsIdentity.GetCurrent()).IsInRole(System.Security.Principal.WindowsBuiltInRole.Administrator))throw new Exception("请以普通方式打开本程序；启动助手会在需要时单独请求权限。");
            var games=Process.GetProcessesByName("Lumin");try {if(games.Length!=0)throw new Exception("游戏正在运行。请正常退出后，再点击启动。");}finally{foreach(var g in games)g.Dispose();}
            sessionId=Guid.NewGuid().ToString("N");helperStatePath=Path.Combine(Control,"launch_"+sessionId+".json");
            SetSharedEffects();
            targetPid=0;targetStartUtc=null;commandId=0;uriOpened=false;lastState=null;lastRejectReason=null;wantReplacement=true;
            string helperArgs;using(var self=Process.GetCurrentProcess())helperArgs="--arm "+sessionId+" "+self.Id+" "+self.StartTime.ToUniversalTime().Ticks;
            var pi=new ProcessStartInfo(SafePath(package.helperPath),helperArgs){UseShellExecute=true,Verb="runas",WindowStyle=ProcessWindowStyle.Hidden};
            using(var child=Process.Start(pi)){}helperPending=true;start.Enabled=false;
            Status("正在准备启动","请完成 Windows 权限确认。启动助手准备就绪后，将按管理器设置启动游戏。","进入莉特莉丝「绮夜寐影」服饰页查看。");Note("已请求启动准备，每次点击只尝试一次。");
        }catch(System.ComponentModel.Win32Exception e){Note("启动权限未确认："+e.Message);Status("本次启动已取消","没有开始新的替换会话。","可稍后重新点击启动。");}
        catch(Exception e){Note(e.Message);Status("暂未启动",e.Message,"原模型保持正常。");}
    }
    void WriteCommand(string mode) {
        if(targetPid<=0||String.IsNullOrEmpty(sessionId)||package==null)return;
        long now=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        Atomic(Path.Combine(Control,"request.json"),new{version=1,sessionId=sessionId,commandId=++commandId,contractSha256=package.contractSha256,mode=mode,enabledUntilUnixMs=mode=="replace"?now+45000:now+300000,targetPid=targetPid,targetStartUtc=targetStartUtc});
        lastRenew=DateTime.UtcNow;lastCommandAt=lastRenew;
    }
    void Restore() {
        wantReplacement=false;
        if(targetPid==0){Status("当前没有试用会话","本窗口尚未连接正在运行的试用。","");return;}
        try{
            WriteCommand("original");
            Status("等待停止回执","命令会停止后续换色。","已上传的服装和头部颜色须正常退出游戏后恢复。");
            Note("已请求停止后续换色。");
        }catch(Exception e){Note("停止命令写入失败 "+e.Message);}
    }
    void SetSharedEffects() {
        if(String.IsNullOrEmpty(sessionId))return;
        var path=Path.Combine(Control,"shared_"+sessionId+".json");
        try{Atomic(path,sharedEffects.Checked?"on":"off");}
        catch(Exception e){Note("无法保存单人特效开关："+e.Message);}
    }
    void Tick() {
        if(closing||String.IsNullOrEmpty(sessionId))return;
        try {
            if(File.Exists(helperStatePath)) {
                var h=ReadState<HelperState>(helperStatePath);if(h==null)return;
                if(h.sessionId!=sessionId)throw new Exception("启动状态所属会话不匹配。");
                if(h.state!=lastState){lastState=h.state;Note("启动状态："+h.state+(String.IsNullOrEmpty(h.error)?"":" / "+h.error));}
                if(h.state=="armed"&&!uriOpened){GameLocation.Read(Root).Launch();uriOpened=true;Status("等待游戏启动",GameLocation.Read(Root).launchMode=="manual"?"加载助手已就绪，请现在正常启动游戏并进入莉特莉丝角色页。":"已按所选方式请求正常启动。进入莉特莉丝角色页后会检查适配条件。","启动等待最长3分钟，不会自动重试。");}
                if(h.state=="loaded"&&targetPid==0){if(h.targetPid<1||String.IsNullOrEmpty(h.targetStartUtc))throw new Exception("启动记录缺少目标进程。");targetPid=h.targetPid;targetStartUtc=h.targetStartUtc;helperPending=false;WriteCommand(wantReplacement?"replace":"original");Status("等待服装贴图","已加载服装换色组件。请选择莉特莉丝「绮夜寐影」。","原配色保持至匹配到贴图。");}
                if(h.state=="failed"){helperPending=false;wantReplacement=false;start.Enabled=package.runtimeReady;Status("本次启动已停止",h.error??"详见验证记录。","不会自动重新加载或重启游戏。");}
            }
            if(targetPid>0) {
                bool alive=false;try{using(var p=Process.GetProcessById(targetPid)){alive=p.StartTime.ToUniversalTime().ToString("o")==targetStartUtc;}}catch{}
                if(!alive){wantReplacement=false;targetPid=0;helperPending=false;start.Enabled=package.runtimeReady;Status("游戏会话已结束","可再次点击启动，开始新的试用会话。","恢复设置不会延续到另一游戏进程。");return;}
                if(wantReplacement&&(DateTime.UtcNow-lastRenew).TotalSeconds>=10)WriteCommand("replace");
                string stateFile=Path.Combine(Control,"status_"+targetPid+".json");
                if(File.Exists(stateFile)) {
                    var a=ReadState<AddonState>(stateFile);if(a==null)return;
                    DateTime modified=File.GetLastWriteTimeUtc(stateFile);
                    if(a.version!=1||a.pid!=targetPid||a.sessionId!=sessionId||a.targetStartUtc!=targetStartUtc||a.contractSha256!=package.contractSha256||a.commandId!=commandId||modified<lastCommandAt.AddSeconds(-1)||(DateTime.UtcNow-modified).TotalSeconds>15){Status("等待游戏端回执","尚未收到与最新命令匹配的状态。","状态文件过期时不会显示为替换或恢复成功。");return;}
                    string counts="上身与附件 "+a.outfitBody01Mips+" 层 · 下装 "+a.outfitBody02Mips+" 层 · 银发 "+a.hairSilverMips+" 层 · 淡红受光 "+a.hairLightDraws+" 笔 · 手套受光 "+a.gloveLightDraws+" 笔 · 技能受光 "+a.surfaceLightDraws+" 笔 · 无人机 "+a.weaponMips+" 次 · 战斗候选 "+a.groundGuardSubmissions+" · 预览 "+a.primaryPreviewSubmissions+(a.primaryPreviewActive?"（已开启）":a.primaryPreviewWaiting?"（等待出场）":"");
                    if(a.trackingFailed) {
                        if(wantReplacement){wantReplacement=false;try{WriteCommand("original");}catch{}}
                        Status("本次试用已停止","服装换色组件未能继续工作。","请正常退出游戏后重新试用。");
                    } else if(a.mode=="original"&&!wantReplacement) {
                        Status("已停止后续换色",a.outfitAlreadyUploaded?"已上传的服装和头部颜色须正常退出游戏后恢复。":"本次没有替换任何服装贴图。",counts);
                    } else if(a.mode=="replace"&&a.outfitBody01Mips>0&&a.outfitBody02Mips>0) {
                        Status("服装已换入","银发与淡红受光以计数及实际画面为准。",counts);
                    } else {
                        Status("等待服装贴图",a.outfitUploadReady?"请选择绮夜寐影并等待服装加载。":"正在等待图形设备初始化。",counts);
                    }
                    if(!String.IsNullOrEmpty(a.rejectReason)&&lastRejectReason!=a.rejectReason){
                        lastRejectReason=a.rejectReason;Note("贴图状态 "+a.rejectReason+" · "+counts);
                    }
                }
            }
        }catch(Exception e){wantReplacement=false;CancelSession();try{WriteCommand("original");}catch{}sessionId=null;helperPending=false;Status("已停止替换请求",e.Message,"后续换色授权会到期。正常退出游戏后恢复原配色。");Note(e.Message);}
    }
    void OpenLocal(string relative){try{if(String.IsNullOrEmpty(relative))throw new Exception("此构建尚未生成该页面。");Process.Start(new ProcessStartInfo(SafePath(relative)){UseShellExecute=true});}catch(Exception e){Note(e.Message);}}
    [STAThread]static int Main(string[] args) {
        Directory.CreateDirectory(Logs);
        if(args.Length==1&&(args[0]=="--check-only"||args[0]=="--check-runtime-only")) {try{var p=ValidatePackage(args[0]=="--check-runtime-only");Atomic(Path.Combine(Logs,"check_only.json"),new{passed=true,runtimeReady=p.runtimeReady,liveGameVerified=p.liveGameVerified,gameLaunched=false,adminRequested=false});return 0;}catch(Exception e){Atomic(Path.Combine(Logs,"check_only.json"),new{passed=false,error=e.Message,gameLaunched=false,adminRequested=false});return 2;}}
        bool created;using(var single=new System.Threading.Mutex(true,@"Local\LvmengModelTool_v1",out created)){
            if(!created){MessageBox.Show("模型替换程序已经打开，请使用现有窗口。","莉特莉丝 · 红黑收尾修正版");return 2;}
            try{Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Application.Run(new ModelTool());return 0;}finally{single.ReleaseMutex();}
        }
    }
}
