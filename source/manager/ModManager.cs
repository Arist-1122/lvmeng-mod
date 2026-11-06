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

public sealed class Skin { public string id, name; }
public sealed class Character { public string id, name; public List<Skin> skins = new List<Skin>(); }
public sealed class Settings {
    public int version = 1;
    public string gameExecutable = "", launchMode = "taptap";
    public List<Character> characters = new List<Character>();
    public List<string> enabledModIds = new List<string>();
}
public sealed class ModSpec {
    public int version;
    public string id, name, characterId, skinId, characterName, skinName, runtimeDirectory, entryPoint, packageManifest;
}
public sealed class PackageFile { public string path, sha256; }
public sealed class RuntimePackage {
    public int version; public bool runtimeReady, liveGameVerified;
    public string helperPath, loaderPath, corePath, bootstrapPath, addonPath, contractPath, configTemplatePath, contractSha256;
    public PackageFile[] files, gameSamples;
}
public sealed class InstalledMod {
    public string folder; public ModSpec spec; public RuntimePackage package;
    public string Runtime { get { return Store.Child(folder, spec.runtimeDirectory); } }
}
public sealed class Store {
    public readonly string root;
    public Settings settings;
    public readonly List<string> errors = new List<string>();
    public static readonly JavaScriptSerializer Json = new JavaScriptSerializer { MaxJsonLength = 2097152 };
    static readonly UTF8Encoding Utf8 = new UTF8Encoding(false);
    public Store(string path) {
        root = Path.GetFullPath(path); Directory.CreateDirectory(Path.Combine(root, "mods")); Directory.CreateDirectory(Path.Combine(root, "user-data"));
        string p = Path.Combine(root, "user-data", "settings.json");
        bool fresh = !File.Exists(p);
        settings = fresh ? new Settings() : Read<Settings>(p);
        if (settings.version != 1 || settings.characters == null || settings.enabledModIds == null) throw new Exception("管理器配置版本无效。");
        if (fresh) {
            var defaults = List();
            foreach (var m in defaults) {
                RegisterTarget(m);
                if (!defaults.Any(other => settings.enabledModIds.Contains(other.spec.id) && other.spec.characterId == m.spec.characterId && other.spec.skinId == m.spec.skinId)) settings.enabledModIds.Add(m.spec.id);
            }
        }
    }
    public static T Read<T>(string path) { if (new FileInfo(path).Length > 2097152) throw new Exception("JSON 文件过大。"); return Json.Deserialize<T>(File.ReadAllText(path, Utf8)); }
    public static void Write(string path, object value) {
        Directory.CreateDirectory(Path.GetDirectoryName(path)); string temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try { File.WriteAllText(temp, Json.Serialize(value), Utf8); if (File.Exists(path)) File.Replace(temp, path, null); else File.Move(temp, path); } finally { if (File.Exists(temp)) File.Delete(temp); }
    }
    public void Save() { Write(Path.Combine(root, "user-data", "settings.json"), settings); }
    void RegisterTarget(InstalledMod mod) {
        var c = settings.characters.FirstOrDefault(x => x.id == mod.spec.characterId);
        if (c == null) { c = new Character { id = mod.spec.characterId, name = mod.spec.characterName ?? mod.spec.characterId }; settings.characters.Add(c); }
        if (!c.skins.Any(s => s.id == mod.spec.skinId)) c.skins.Add(new Skin { id = mod.spec.skinId, name = mod.spec.skinName ?? mod.spec.skinId });
    }
    public static bool Id(string value) { return value != null && System.Text.RegularExpressions.Regex.IsMatch(value, "^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,79}$") && value != "." && value != ".."; }
    public static string Child(string path, string relative) {
        if (String.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative) || relative.IndexOf(':') >= 0) throw new Exception("包内路径必须是相对路径。");
        string prefix = Path.GetFullPath(path).TrimEnd('\\') + "\\";
        string full = Path.GetFullPath(Path.Combine(prefix, relative));
        if (!full.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) throw new Exception("包内路径越界。");
        string current = full;
        while (current.Length >= prefix.Length) { if ((File.Exists(current) || Directory.Exists(current)) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0) throw new Exception("包内不允许链接或重解析点。"); current = Path.GetDirectoryName(current); if (current == null) break; }
        return full;
    }
    public static string Hash(string path) { using (var sha = SHA256.Create()) using (var f = File.OpenRead(path)) return BitConverter.ToString(sha.ComputeHash(f)).Replace("-", "").ToLowerInvariant(); }
    public InstalledMod Inspect(string folder, bool checkFiles) {
        folder = Path.GetFullPath(folder); var m = Read<ModSpec>(Path.Combine(folder, "mod.json"));
        if (m == null || m.version != 1 || !Id(m.id) || !Id(m.characterId) || !Id(m.skinId) || String.IsNullOrWhiteSpace(m.name)) throw new Exception("Mod 清单无效。");
        string runtime = Child(folder, m.runtimeDirectory), manifest = Child(runtime, m.packageManifest), entry = Child(runtime, m.entryPoint);
        if (!Path.GetExtension(entry).Equals(".exe", StringComparison.OrdinalIgnoreCase)) throw new Exception("运行器必须是 EXE。");
        var p = Read<RuntimePackage>(manifest);
        if (p == null || p.version != 1 || p.files == null || p.files.Length < 1 || p.files.Length > 1024 || p.gameSamples == null) throw new Exception("运行包清单无效。");
        var listed = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var f in p.files) {
            if (f == null || f.sha256 == null || !System.Text.RegularExpressions.Regex.IsMatch(f.sha256, "^[0-9a-f]{64}$")) throw new Exception("文件摘要格式无效。");
            string file = Child(runtime, f.path); if (listed.ContainsKey(file)) throw new Exception("重复包内路径。"); listed.Add(file, f.sha256);
            if (checkFiles && (!File.Exists(file) || Hash(file) != f.sha256)) throw new Exception("文件校验失败：" + f.path);
        }
        if (!listed.ContainsKey(entry)) throw new Exception("运行器未纳入校验清单。");
        if (p.runtimeReady) {
            foreach (var name in new[] { p.helperPath, p.loaderPath, p.corePath, p.bootstrapPath, p.addonPath, p.contractPath, p.configTemplatePath }) if (!listed.ContainsKey(Child(runtime, name))) throw new Exception("运行合同缺少必要文件。");
            if (listed[Child(runtime, p.contractPath)] != p.contractSha256) throw new Exception("运行合同摘要不一致。");
            if (p.gameSamples.Length == 0) throw new Exception("缺少游戏版本样本。");
        }
        foreach (var f in p.gameSamples) Child(runtime, f.path); // Enforce portable relative sample paths.
        return new InstalledMod { folder = folder, spec = m, package = p };
    }
    public List<InstalledMod> List() {
        var result = new List<InstalledMod>(); errors.Clear();
        foreach (var folder in Directory.GetDirectories(Path.Combine(root, "mods"))) { if (Path.GetFileName(folder).StartsWith(".")) continue; try { result.Add(Inspect(folder, false)); } catch (Exception e) { errors.Add(Path.GetFileName(folder) + "：" + e.Message); } }
        return result;
    }
    public void AddCharacter(string id, string name, string skinId, string skinName) {
        if (!Id(id) || !Id(skinId) || String.IsNullOrWhiteSpace(name) || String.IsNullOrWhiteSpace(skinName)) throw new Exception("角色和皮肤的 ID 或名称无效。");
        var c = settings.characters.FirstOrDefault(x => x.id == id);
        if (c == null) { c = new Character { id = id, name = name }; settings.characters.Add(c); }
        if (c.skins.Any(x => x.id == skinId)) throw new Exception("这个角色已存在同 ID 皮肤。");
        c.skins.Add(new Skin { id = skinId, name = skinName }); Save();
    }
    public InstalledMod Import(string source) {
        var m = Inspect(source, true); string dest = Child(Path.Combine(root, "mods"), m.spec.id);
        if (Directory.Exists(dest)) throw new Exception("这个 Mod ID 已导入；请使用新的版本 ID。");
        string stage = Child(Path.Combine(root, "mods"), ".import-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(stage);
        try {
            File.Copy(Path.Combine(source, "mod.json"), Path.Combine(stage, "mod.json"));
            string runtime = Child(stage, m.spec.runtimeDirectory); Directory.CreateDirectory(runtime);
            var names = m.package.files.Select(x => x.path).Concat(new[] { m.spec.packageManifest }).Distinct(StringComparer.OrdinalIgnoreCase);
            foreach (string name in names) { string to = Child(runtime, name); Directory.CreateDirectory(Path.GetDirectoryName(to)); File.Copy(Child(m.Runtime, name), to); }
            Inspect(stage, true); Directory.Move(stage, dest);
            RegisterTarget(m); Save();
            return Inspect(dest, true);
        } finally { if (Directory.Exists(stage)) Directory.Delete(stage, true); }
    }
    public void Enable(InstalledMod mod, bool enabled) {
        settings.enabledModIds.Remove(mod.spec.id);
        if (enabled) { var same = List().Where(m => m.spec.characterId == mod.spec.characterId && m.spec.skinId == mod.spec.skinId).Select(m => m.spec.id).ToArray(); settings.enabledModIds.RemoveAll(id => same.Contains(id)); settings.enabledModIds.Add(mod.spec.id); }
        Save();
    }
    public void Configure(InstalledMod mod) {
        var cfg = new GameLocation { gameExecutable = settings.gameExecutable, launchMode = settings.launchMode };
        Write(Path.Combine(mod.Runtime, "game-location.json"), cfg);
    }
    public GameCompatibility.Result ValidateGame(InstalledMod mod) {
        Configure(mod); var game = GameLocation.Read(mod.Runtime);
        return GameCompatibility.Validate(mod.Runtime, game.gameExecutable);
    }
}

public sealed class CharacterDialog : Form {
    TextBox[] boxes = new TextBox[4];
    public string[] Values { get { return boxes.Select(x => x.Text.Trim()).ToArray(); } }
    public CharacterDialog() {
        Text = "添加角色 / 皮肤"; ClientSize = new Size(490, 282); FormBorderStyle = FormBorderStyle.FixedDialog; MaximizeBox = false; MinimizeBox = false; StartPosition = FormStartPosition.CenterParent; Font = new Font("Microsoft YaHei UI", 10);
        string[] labels = { "角色 ID", "角色显示名称", "皮肤 ID", "皮肤显示名称" };
        for (int i = 0; i < 4; ++i) { Controls.Add(new Label { Text = labels[i], AutoSize = true, Location = new Point(20, 24 + i * 42) }); boxes[i] = new TextBox { Location = new Point(155, 20 + i * 42), Width = 310 }; Controls.Add(boxes[i]); }
        Controls.Add(new Label { Text = "ID 须与 Mod 清单一致；同角色 ID 可继续添加皮肤。", AutoSize = true, Location = new Point(20, 196), ForeColor = Color.DimGray });
        var ok = new Button { Text = "添加", DialogResult = DialogResult.OK, Location = new Point(260, 238), Width = 95 }; var cancel = new Button { Text = "取消", DialogResult = DialogResult.Cancel, Location = new Point(370, 238), Width = 95 }; Controls.Add(ok); Controls.Add(cancel); AcceptButton = ok; CancelButton = cancel;
    }
}

public sealed class ManagerForm : Form {
    Store store; ListView mods; TextBox game, details; ComboBox launch; Label count; List<InstalledMod> installed; Dictionary<string, Process> runners = new Dictionary<string, Process>();
    public ManagerForm(Store s) {
        store = s; Text = "绿梦 Mod 管理器 · v0.1.2 预览版"; Font = new Font("Microsoft YaHei UI", 10); ClientSize = new Size(1010, 680); MinimumSize = new Size(1026, 719); StartPosition = FormStartPosition.CenterScreen; BackColor = Color.FromArgb(242, 244, 248);
        var title = new Label { Text = "绿梦 Mod 管理器 · v0.1.2 预览版", Location = new Point(25, 20), AutoSize = true, Font = new Font(Font.FontFamily, 22, FontStyle.Bold) }; Controls.Add(title);
        Controls.Add(new Label { Text = "角色与皮肤独立管理 · 每次启动一个已适配的 Mod", AutoSize = true, Location = new Point(28, 65), ForeColor = Color.DimGray });
        Controls.Add(new Label { Text = "游戏 Lumin.exe", AutoSize = true, Location = new Point(28, 111) });
        game = new TextBox { Text = s.settings.gameExecutable, Location = new Point(155, 107), Width = 685 }; Controls.Add(game);
        AddButton("选择…", 855, 104, 120, delegate { using (var d = new OpenFileDialog { Filter = "绿梦游戏本体|Lumin.exe", Title = "选择安装目录中的 Lumin.exe" }) if (d.ShowDialog() == DialogResult.OK) { game.Text = d.FileName; SaveLocation(); } });
        Controls.Add(new Label { Text = "启动方式", AutoSize = true, Location = new Point(28, 153) });
        launch = new ComboBox { Location = new Point(155, 147), Width = 210, DropDownStyle = ComboBoxStyle.DropDownList }; launch.Items.AddRange(new object[] { "通过 TapTap", "直接启动所选 EXE", "手动启动游戏" }); launch.SelectedIndex = s.settings.launchMode == "direct" ? 1 : s.settings.launchMode == "manual" ? 2 : 0; Controls.Add(launch);
        AddButton("保存设置", 385, 145, 135, SaveLocation);
        AddButton("添加角色 / 皮肤", 540, 145, 180, AddCharacter);
        AddButton("导入 Mod", 740, 145, 120, Import);
        AddButton("刷新", 875, 145, 100, Reload);
        mods = new ListView { Location = new Point(28, 203), Size = new Size(947, 272), View = View.Details, FullRowSelect = true, MultiSelect = false, HideSelection = false, Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right };
        mods.Columns.Add("状态", 80); mods.Columns.Add("Mod", 290); mods.Columns.Add("角色", 190); mods.Columns.Add("皮肤", 190); mods.Columns.Add("验证", 170); mods.SelectedIndexChanged += delegate { Selection(); }; Controls.Add(mods);
        AddButton("启用所选", 28, 492, 135, delegate { Toggle(true); }); AddButton("停用所选", 178, 492, 135, delegate { Toggle(false); });
        AddButton("检查包和游戏版本", 328, 492, 200, Check); AddButton("打开运行器", 543, 492, 180, OpenRunner); AddButton("打开 Mod 文件夹", 738, 492, 237, delegate { var m = Selected(); Process.Start(new ProcessStartInfo(m.folder) { UseShellExecute = true }); });
        details = new TextBox { Location = new Point(28, 549), Size = new Size(947, 80), ReadOnly = true, Multiline = true, ScrollBars = ScrollBars.Vertical, BackColor = Color.White, Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right }; Controls.Add(details);
        count = new Label { Location = new Point(28, 645), AutoSize = true, Anchor = AnchorStyles.Bottom | AnchorStyles.Left, ForeColor = Color.DimGray }; Controls.Add(count); Reload();
    }
    void AddButton(string name, int x, int y, int width, Action action) { var b = new Button { Text = name, Location = new Point(x, y), Size = new Size(width, 36), FlatStyle = FlatStyle.Flat, BackColor = Color.White }; b.Click += delegate { try { action(); } catch (Exception e) { MessageBox.Show(this, e.Message, "操作未完成", MessageBoxButtons.OK, MessageBoxIcon.Information); } }; Controls.Add(b); }
    void Note(string text) { details.Text = text; }
    InstalledMod Selected() { if (mods.SelectedItems.Count != 1) throw new Exception("请先选择一个 Mod。"); return (InstalledMod)mods.SelectedItems[0].Tag; }
    void Reload() {
        mods.Items.Clear(); installed = store.List();
        foreach (var m in installed) { var c = store.settings.characters.FirstOrDefault(x => x.id == m.spec.characterId); var skin = c == null ? null : c.skins.FirstOrDefault(x => x.id == m.spec.skinId); var row = new ListViewItem(new[] { store.settings.enabledModIds.Contains(m.spec.id) ? "已启用" : "已停用", m.spec.name, c == null ? (m.spec.characterName ?? m.spec.characterId) : c.name, skin == null ? (m.spec.skinName ?? m.spec.skinId) : skin.name, m.package.liveGameVerified ? "实机记录见说明" : "待实机验收" }) { Tag = m }; mods.Items.Add(row); }
        count.Text = installed.Count + " 个 Mod · " + installed.Select(m => m.spec.characterId).Concat(store.settings.characters.Select(c => c.id)).Distinct().Count() + " 个角色 · 导入 Mod 文件夹内的 mod.json";
        Note(store.errors.Count > 0 ? "以下目录未能载入，请修复或移出 mods 文件夹：\r\n" + String.Join("\r\n", store.errors) : "导入独立适配的 Mod，再启用并打开运行器。管理器不会自动推断其他角色的材质与模型；每个 Mod 需提供自己的校验清单。");
    }
    void Selection() { if (mods.SelectedItems.Count == 1) { var m = Selected(); Note(m.spec.name + "\r\nID: " + m.spec.id + " · 角色: " + m.spec.characterId + " · 皮肤: " + m.spec.skinId + "\r\n" + m.folder); } }
    void SaveLocation() { string exe = game.Text.Trim(); if (exe.Length > 0 && (!Path.IsPathRooted(exe) || !File.Exists(exe) || !Path.GetFileName(exe).Equals("Lumin.exe", StringComparison.OrdinalIgnoreCase))) throw new Exception("请选择存在的 Lumin.exe。"); store.settings.gameExecutable = exe.Length == 0 ? "" : Path.GetFullPath(exe); store.settings.launchMode = launch.SelectedIndex == 1 ? "direct" : launch.SelectedIndex == 2 ? "manual" : "taptap"; store.Save(); Note("已保存游戏位置和启动方式。下次打开运行器时会重新校验客户端版本。"); }
    void AddCharacter() { using (var d = new CharacterDialog()) if (d.ShowDialog(this) == DialogResult.OK) { store.AddCharacter(d.Values[0], d.Values[1], d.Values[2], d.Values[3]); Reload(); } }
    void Import() { using (var d = new OpenFileDialog { Filter = "Mod 清单|mod.json", Title = "选择解压后 Mod 文件夹中的 mod.json" }) if (d.ShowDialog(this) == DialogResult.OK) { UseWaitCursor = true; try { var m = store.Import(Path.GetDirectoryName(d.FileName)); Reload(); Note("已导入并校验：" + m.spec.name + "。角色与皮肤已登记；新导入项默认停用，点击启用后可打开运行器。"); } finally { UseWaitCursor = false; } } }
    void Toggle(bool enabled) { var m = Selected(); store.Enable(m, enabled); Process p; if (!enabled && runners.TryGetValue(m.spec.id, out p) && !p.HasExited) p.CloseMainWindow(); Reload(); Note(enabled ? "已启用。相同角色、相同皮肤的其他 Mod 已停用；点击打开运行器开始。" : "已停用，并请求关闭本管理器打开的运行器。已上传贴图须正常退出游戏后恢复。"); }
    void Check() { var m = Selected(); SaveLocation(); UseWaitCursor = true; try { m = store.Inspect(m.folder, true); var compatibility = store.ValidateGame(m); Note("包内文件校验通过。" + compatibility.message); } finally { UseWaitCursor = false; } }
    void OpenRunner() { var m = Selected(); if (!store.settings.enabledModIds.Contains(m.spec.id)) throw new Exception("请先启用这个 Mod。"); SaveLocation(); m = store.Inspect(m.folder, true); if (!m.package.runtimeReady) throw new Exception("此 Mod 尚未开放运行。"); var compatibility = store.ValidateGame(m); Process old; if (runners.TryGetValue(m.spec.id, out old) && !old.HasExited) throw new Exception("这个 Mod 的运行器已打开。"); string entry = Store.Child(m.Runtime, m.spec.entryPoint); runners[m.spec.id] = Process.Start(new ProcessStartInfo(entry) { WorkingDirectory = m.Runtime, UseShellExecute = true }); Note("已打开 " + m.spec.name + " 的运行器。" + compatibility.message); }
    static void CreateHandles(Control control) { var handle = control.Handle; foreach (Control c in control.Controls) CreateHandles(c); }
    [STAThread] public static int Main(string[] args) {
        if (args.Length == 3 && args[0] == "--check-compatibility") return GameCompatibility.RunReadOnlyCli(args[1], args[2]);
        if (args.Length == 2 && args[0] == "--self-test") return SelfTests.Run(args[1]);
        if (args.Length == 2 && args[0] == "--verify-mod") { try { var s = new Store(AppDomain.CurrentDomain.BaseDirectory); var m = s.Inspect(args[1], true); Store.Write(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "verify-result.json"), new { passed = true, id = m.spec.id, files = m.package.files.Length, gameLaunched = false, loaderInvoked = false }); return 0; } catch (Exception e) { Store.Write(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "verify-result.json"), new { passed = false, error = e.Message, gameLaunched = false }); return 2; } }
        if (args.Length == 3 && args[0] == "--render-preview") { Application.EnableVisualStyles(); using (var f = new ManagerForm(new Store(args[1]))) { f.ShowInTaskbar = false; CreateHandles(f); using (var bitmap = new Bitmap(f.Width, f.Height)) { f.DrawToBitmap(bitmap, new Rectangle(0, 0, bitmap.Width, bitmap.Height)); bitmap.Save(args[2], System.Drawing.Imaging.ImageFormat.Png); } } return 0; }
        try { Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false); Application.Run(new ManagerForm(new Store(AppDomain.CurrentDomain.BaseDirectory))); return 0; } catch (Exception e) { MessageBox.Show(e.Message, "绿梦 Mod 管理器"); return 2; }
    }
}
