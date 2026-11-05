using System;
using System.IO;
using System.Web.Script.Serialization;

// User-local configuration, deliberately outside the immutable package file list.
public sealed class GameLocation {
    public int version = 1;
    public string gameExecutable = "";
    public string launchMode = "taptap";
    public string launchUri = "taptap://taptap.com/app?app_id=237561&auto_launch=true&platform=pc";
    public static GameLocation Read(string runtimeRoot) {
        string file = Path.Combine(runtimeRoot, "game-location.json");
        if (!File.Exists(file)) throw new Exception("请在 Mod 管理器中设置游戏 Lumin.exe 的位置。");
        if (new FileInfo(file).Length > 16384) throw new Exception("游戏位置配置过大。");
        var p = new JavaScriptSerializer().Deserialize<GameLocation>(File.ReadAllText(file));
        if (p == null || p.version != 1 || String.IsNullOrWhiteSpace(p.gameExecutable) || !Path.IsPathRooted(p.gameExecutable)) throw new Exception("游戏位置配置无效。");
        p.gameExecutable = Path.GetFullPath(p.gameExecutable);
        if (!Path.GetFileName(p.gameExecutable).Equals("Lumin.exe", StringComparison.OrdinalIgnoreCase)) throw new Exception("请选择游戏本体 Lumin.exe。");
        if (!File.Exists(p.gameExecutable)) throw new Exception("找不到游戏文件：" + p.gameExecutable);
        if (p.launchMode != "taptap" && p.launchMode != "direct" && p.launchMode != "manual") throw new Exception("启动方式配置无效。");
        if (p.launchMode == "taptap" && p.launchUri != "taptap://taptap.com/app?app_id=237561&auto_launch=true&platform=pc") throw new Exception("TapTap 启动地址不匹配。");
        return p;
    }
    public string Sample(string relative) {
        if (String.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative)) throw new Exception("客户端样本必须为相对路径。");
        string root = Path.GetDirectoryName(gameExecutable).TrimEnd('\\') + "\\";
        string path = Path.GetFullPath(Path.Combine(root, relative));
        if (!path.StartsWith(root, StringComparison.OrdinalIgnoreCase)) throw new Exception("客户端样本路径越界。");
        return path;
    }
    public void Launch() {
        if (launchMode == "manual") return;
        var p = new System.Diagnostics.ProcessStartInfo(launchMode == "direct" ? gameExecutable : launchUri) { UseShellExecute = true, WorkingDirectory = Path.GetDirectoryName(gameExecutable) };
        System.Diagnostics.Process.Start(p);
    }
}
