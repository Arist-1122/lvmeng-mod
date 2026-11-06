using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;

// No account, UAC or token settings are changed. A normal desktop token is
// borrowed only to create a bounded, same-user game-launch bridge process.
public static class LaunchPrivileges {
    public sealed class Snapshot {
        public bool currentAdministrator, shellInspected, shellPresent, shellAdministrator, sameUser, sameSession;
        public int currentSession, shellSession, shellPid;
        public string route, message;
    }
    const uint QueryLimited=0x1000, TokenQuery=0x0008, TokenDuplicate=0x0002, TokenAssignPrimary=0x0001;
    const uint CreateUnicodeEnvironment=0x00000400, CreateNoWindow=0x08000000, WaitTimeout=258, WaitFailed=0xffffffff;
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]
    struct StartupInfo {
        public int cb; public string reserved,desktop,title;
        public int x,y,xSize,ySize,xCountChars,yCountChars,fillAttribute,flags;
        public short showWindow,reserved2; public IntPtr reserved2Ptr,stdInput,stdOutput,stdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct ProcessInformation {public IntPtr process,thread; public uint processId,threadId;}
    [StructLayout(LayoutKind.Sequential)]
    struct FileTime {public uint low,high; public long Value {get{return ((long)high<<32)|low;}}}
    [DllImport("user32.dll")] static extern IntPtr GetShellWindow();
    [DllImport("user32.dll",SetLastError=true)] static extern uint GetWindowThreadProcessId(IntPtr window,out uint processId);
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,uint processId);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool ProcessIdToSessionId(uint processId,out uint sessionId);
    [DllImport("kernel32.dll",SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle,uint milliseconds);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr process,out uint code);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr process,out FileTime created,out FileTime exited,out FileTime kernel,out FileTime user);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool QueryFullProcessImageName(IntPtr process,uint flags,StringBuilder name,ref uint size);
    [DllImport("advapi32.dll",SetLastError=true)] static extern bool OpenProcessToken(IntPtr process,uint access,out IntPtr token);
    [DllImport("advapi32.dll",SetLastError=true)] static extern bool DuplicateTokenEx(IntPtr token,uint access,IntPtr attributes,int impersonationLevel,int tokenType,out IntPtr duplicate);
    [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern bool CreateProcessWithTokenW(IntPtr token,uint logonFlags,string application,StringBuilder commandLine,uint flags,IntPtr environment,string directory,ref StartupInfo startup,out ProcessInformation information);
    [DllImport("userenv.dll",SetLastError=true)] static extern bool CreateEnvironmentBlock(out IntPtr environment,IntPtr token,bool inherit);
    [DllImport("userenv.dll",SetLastError=true)] static extern bool DestroyEnvironmentBlock(IntPtr environment);
    static Exception NativeError(string operation){return new Win32Exception(Marshal.GetLastWin32Error(),operation);}
    public static bool IsAdministrator() {using(var identity=WindowsIdentity.GetCurrent())return new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator);}

    // Pure policy seam: tests cover both UAC desktops and full-token desktops
    // without changing system settings or creating a game process.
    public static string ChooseRoute(bool administrator,bool shellPresent,bool sameSession,bool sameUser,bool shellAdministrator) {
        if(!administrator)return "current-user";
        if(!shellPresent)return "existing-environment";
        if(!sameSession)throw new Exception("桌面与运行器不在同一交互会话，请在游戏所在桌面会话重新打开管理器。");
        if(!sameUser)throw new Exception("桌面与运行器属于不同用户，请用桌面同一用户重新打开管理器。");
        return shellAdministrator?"existing-environment":"desktop-user";
    }
    static IntPtr OpenShellToken(int pid,bool duplicate) {
        IntPtr process=OpenProcess(QueryLimited,false,(uint)pid);
        if(process==IntPtr.Zero)throw NativeError("无法读取桌面进程权限；没有改用管理员权限启动游戏，请选择手动启动。");
        try {IntPtr token;if(!OpenProcessToken(process,TokenQuery|(duplicate?TokenDuplicate:0),out token))throw NativeError("无法读取桌面用户令牌；没有改用管理员权限启动游戏，请选择手动启动。");return token;}
        finally {CloseHandle(process);}
    }
    public static Snapshot Inspect() {
        var result=new Snapshot();
        using(var current=Process.GetCurrentProcess())result.currentSession=current.SessionId;
        using(var identity=WindowsIdentity.GetCurrent()) {
            result.currentAdministrator=new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator);
            // Ordinary callers already provide the desired launch context;
            // they do not need token access to an unrelated desktop process.
            if(!result.currentAdministrator){result.route="current-user";result.message="运行器使用普通权限，按当前用户正常启动游戏。";return result;}
            result.shellInspected=true;
            IntPtr shell=GetShellWindow();uint pid=0,session=0;
            if(shell!=IntPtr.Zero) {
                if(GetWindowThreadProcessId(shell,out pid)==0||pid==0)throw NativeError("无法确认桌面进程，未发起游戏启动。");
                if(!ProcessIdToSessionId(pid,out session))throw NativeError("无法确认桌面会话，未发起游戏启动。");
                result.shellPresent=true;result.shellPid=(int)pid;result.shellSession=(int)session;result.sameSession=result.currentSession==result.shellSession;
                // WindowsPrincipal may duplicate a primary token to check its
                // enabled administrator membership, so retain TOKEN_DUPLICATE.
                IntPtr token=OpenShellToken(result.shellPid,true);
                try {using(var desktopIdentity=new WindowsIdentity(token)) {
                    result.sameUser=identity.User!=null&&identity.User.Equals(desktopIdentity.User);
                    result.shellAdministrator=new WindowsPrincipal(desktopIdentity).IsInRole(WindowsBuiltInRole.Administrator);
                }}finally {CloseHandle(token);}
            }
        }
        result.route=ChooseRoute(result.currentAdministrator,result.shellPresent,result.sameSession,result.sameUser,result.shellAdministrator);
        result.message=result.route=="desktop-user"?"运行器具有管理员权限，游戏启动将交给同一桌面用户的普通权限进程。":result.route=="existing-environment"?(result.shellPresent?"当前桌面本身使用管理员权限；未发现同用户普通桌面令牌，将沿用当前环境权限。":"当前交互会话没有桌面 Shell；将沿用当前环境权限，不自动重启或修改 UAC。"):"运行器使用普通权限，按当前用户正常启动游戏。";
        return result;
    }
    public static bool MatchesProcessIdentity(int pid,long startTicks,string executable) {
        IntPtr process=OpenProcess(QueryLimited|0x00100000,false,(uint)pid);if(process==IntPtr.Zero)return false;
        try {FileTime created,exited,kernel,user;uint length=32768;var image=new StringBuilder((int)length);
            return WaitForSingleObject(process,0)==WaitTimeout&&GetProcessTimes(process,out created,out exited,out kernel,out user)&&DateTime.FromFileTimeUtc(created.Value).Ticks==startTicks&&QueryFullProcessImageName(process,0,image,ref length)&&Path.GetFullPath(image.ToString()).Equals(Path.GetFullPath(executable),StringComparison.OrdinalIgnoreCase);
        }finally {CloseHandle(process);}
    }
    public static int RunDesktopBridge(string executable,string arguments,string workingDirectory) {
        var before=Inspect();if(before.route!="desktop-user")throw new Exception("普通桌面上下文已变化，未发起第二次或管理员游戏启动，请重新检查或使用手动模式。");
        executable=Path.GetFullPath(executable);workingDirectory=Path.GetFullPath(workingDirectory);
        if(!File.Exists(executable))throw new Exception("桌面启动桥接程序不存在。");
        IntPtr source=IntPtr.Zero,primary=IntPtr.Zero,environment=IntPtr.Zero;
        ProcessInformation process=new ProcessInformation();
        try {
            source=OpenShellToken(before.shellPid,true);
            using(var identity=new WindowsIdentity(source)) {
                using(var current=WindowsIdentity.GetCurrent())if(!current.User.Equals(identity.User))throw new Exception("桌面用户已变化，停止本次启动。");
                if(new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator))throw new Exception("桌面令牌已变化，停止本次启动。");
            }
            uint session;if(!ProcessIdToSessionId((uint)before.shellPid,out session)||session!=(uint)before.currentSession)throw new Exception("桌面会话已变化，停止本次启动。");
            if(!DuplicateTokenEx(source,TokenQuery|TokenDuplicate|TokenAssignPrimary,IntPtr.Zero,2,1,out primary))throw NativeError("无法创建普通用户启动令牌；未回退为管理员游戏启动。");
            if(!CreateEnvironmentBlock(out environment,primary,false))throw NativeError("无法创建桌面用户环境；未回退为管理员游戏启动。");
            var startup=new StartupInfo();startup.cb=Marshal.SizeOf(typeof(StartupInfo));
            // lpApplicationName is explicit; only our verified executable path
            // is executable code. Arguments contain fixed flags and numeric IDs.
            var command=new StringBuilder("\""+executable+"\" "+arguments);
            if(command.Length>1023)throw new Exception("桌面启动桥接路径过长，请缩短管理器目录或使用手动启动。");
            if(!CreateProcessWithTokenW(primary,1,executable,command,CreateUnicodeEnvironment|CreateNoWindow,environment,workingDirectory,ref startup,out process))throw NativeError("普通桌面启动失败；未回退为管理员游戏启动，请选择手动启动。");
            uint wait=WaitForSingleObject(process.process,30000);
            if(wait==WaitTimeout)throw new Exception("桌面启动请求尚未返回，已停止等待且不自动重试；请先检查游戏状态。");
            if(wait==WaitFailed)throw NativeError("无法确认桌面启动请求结果，未自动重试。");
            uint code;if(!GetExitCodeProcess(process.process,out code))throw NativeError("无法读取桌面启动结果，未自动重试。");
            return unchecked((int)code);
        }finally {
            if(process.thread!=IntPtr.Zero)CloseHandle(process.thread);if(process.process!=IntPtr.Zero)CloseHandle(process.process);
            if(environment!=IntPtr.Zero)DestroyEnvironmentBlock(environment);if(primary!=IntPtr.Zero)CloseHandle(primary);if(source!=IntPtr.Zero)CloseHandle(source);
        }
    }
}
