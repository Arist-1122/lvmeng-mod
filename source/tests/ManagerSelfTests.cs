using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;

public static class SelfTests {
    static List<object> results = new List<object>();
    static void Require(bool condition, string name) { if (!condition) throw new Exception(name); }
    static void Test(string name, Action action) { try { action(); results.Add(new { name = name, passed = true }); } catch (Exception e) { results.Add(new { name = name, passed = false, error = e.Message }); } }
    static void Reject(Action action) { bool stopped = false; try { action(); } catch { stopped = true; } if (!stopped) throw new Exception("Expected rejection"); }
    public static int Run(string output) {
        Directory.CreateDirectory(output); string work = Path.Combine(Path.GetFullPath(output), "fixture-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(work);
        string source = Path.Combine(work, "source"), runtime = Path.Combine(source, "runtime"), game = Path.Combine(work, "game", "Lumin.exe"); Directory.CreateDirectory(runtime); Directory.CreateDirectory(Path.GetDirectoryName(game));
        File.WriteAllText(game, "OFFLINE-CLIENT-FIXTURE-NEVER-EXECUTED"); File.WriteAllText(Path.Combine(runtime, "runner.exe"), "OFFLINE-RUNNER-FIXTURE-NEVER-EXECUTED"); File.WriteAllText(Path.Combine(runtime, "contract.json"), "{}");
        var package = new RuntimePackage { version = 1, runtimeReady = true, helperPath = "runner.exe", loaderPath = "runner.exe", corePath = "runner.exe", bootstrapPath = "runner.exe", addonPath = "runner.exe", contractPath = "contract.json", configTemplatePath = "contract.json", contractSha256 = Store.Hash(Path.Combine(runtime, "contract.json")), files = new[] { new PackageFile { path = "runner.exe", sha256 = Store.Hash(Path.Combine(runtime, "runner.exe")) }, new PackageFile { path = "contract.json", sha256 = Store.Hash(Path.Combine(runtime, "contract.json")) } }, gameSamples = new[] { new PackageFile { path = "Lumin.exe", sha256 = Store.Hash(game) } } };
        var spec = new ModSpec { version = 1, id = "test-redblack", name = "离线测试", characterId = "role_26", skinId = "role_26_02", characterName = "莉特莉丝", skinName = "绮夜寐影", runtimeDirectory = "runtime", entryPoint = "runner.exe", packageManifest = "package.json" };
        Store.Write(Path.Combine(source, "mod.json"), spec); Store.Write(Path.Combine(runtime, "package.json"), package); File.WriteAllText(Path.Combine(runtime, "unlisted.exe"), "not imported");
        var store = new Store(Path.Combine(work, "manager")); InstalledMod imported = null;
        Store fresh = null;
        Test("First run registers built-in role and skin and enables it", delegate { string path = Path.Combine(work, "fresh"), target = Path.Combine(path, "mods", spec.id); foreach (string file in Directory.GetFiles(source, "*", SearchOption.AllDirectories)) { string to = Path.Combine(target, file.Substring(source.Length).TrimStart('\\')); Directory.CreateDirectory(Path.GetDirectoryName(to)); File.Copy(file, to); } fresh = new Store(path); Require(fresh.settings.enabledModIds.Contains(spec.id), "built-in default disabled"); Require(fresh.settings.characters.Count == 1 && fresh.settings.characters[0].name == "莉特莉丝" && fresh.settings.characters[0].skins[0].name == "绮夜寐影", "built-in target names missing"); });
        Test("User disabled state survives reopening", delegate { fresh.Enable(fresh.List()[0], false); Require(!new Store(fresh.root).settings.enabledModIds.Contains(spec.id), "explicit disabled state overridden"); });
        Test("Add character and skin", delegate { store.AddCharacter("role_26", "莉特莉丝", "role_26_02", "绮夜寐影"); Require(store.settings.characters[0].skins.Count == 1, "skin"); });
        Test("Reject duplicate skin", delegate { Reject(delegate { store.AddCharacter("role_26", "莉特莉丝", "role_26_02", "重复"); }); });
        Test("Validate and import declared files", delegate { imported = store.Import(source); Require(File.Exists(Path.Combine(imported.Runtime, "runner.exe")), "runner missing"); Require(!File.Exists(Path.Combine(imported.Runtime, "unlisted.exe")), "unlisted executable copied"); });
        Test("Reject duplicate mod ID", delegate { Reject(delegate { store.Import(source); }); });
        Test("Enable and persist", delegate { store.Enable(imported, true); Require(new Store(store.root).settings.enabledModIds.Contains(spec.id), "enable missing"); });
        Test("Disable and persist", delegate { store.Enable(imported, false); Require(!new Store(store.root).settings.enabledModIds.Contains(spec.id), "disable missing"); });
        Test("Reject escaping package path", delegate { Reject(delegate { Store.Child(runtime, "../escape.exe"); }); });
        Test("Reject rooted package path", delegate { Reject(delegate { Store.Child(runtime, game); }); });
        Test("Reject alternate data stream", delegate { Reject(delegate { Store.Child(runtime, "a:stream"); }); });
        Test("Reject unmanaged entry point", delegate { spec.entryPoint = "unlisted.exe"; Store.Write(Path.Combine(source, "mod.json"), spec); Reject(delegate { store.Inspect(source, true); }); spec.entryPoint = "runner.exe"; Store.Write(Path.Combine(source, "mod.json"), spec); });
        Test("Reject tampered payload", delegate { string file = Path.Combine(imported.Runtime, "runner.exe"), original = File.ReadAllText(file); File.AppendAllText(file, "tampered"); Reject(delegate { store.Inspect(imported.folder, true); }); File.WriteAllText(file, original); });
        Test("Game path and version resolve from user location", delegate { store.settings.gameExecutable = game; store.settings.launchMode = "manual"; store.ValidateGame(imported); var cfg = GameLocation.Read(imported.Runtime); Require(cfg.gameExecutable == game && cfg.Sample("Lumin.exe") == game, "resolution"); });
        Test("Reject wrong game build", delegate { File.AppendAllText(game, "new-build"); Reject(delegate { store.ValidateGame(imported); }); File.WriteAllText(game, "OFFLINE-CLIENT-FIXTURE-NEVER-EXECUTED"); });
        Test("Reject escaping game sample", delegate { var cfg = GameLocation.Read(imported.Runtime); Reject(delegate { cfg.Sample("../outside.dll"); }); });
        Test("Relocate mod tree and retain validation", delegate { string moved = Path.Combine(work, "relocated mod 中文"); Directory.Move(imported.folder, moved); var relocated = store.Inspect(moved, true); store.Configure(relocated); store.ValidateGame(relocated); });
        bool passed = results.All(x => (bool)x.GetType().GetProperty("passed").GetValue(x, null));
        Store.Write(Path.Combine(output, "manager_validation.json"), new { passed = passed, gameLaunched = false, loaderInvoked = false, adminRequested = false, fixtureRoot = work, tests = results });
        Console.WriteLine(Store.Json.Serialize(new { passed = passed, tests = results.Count, report = Path.Combine(output, "manager_validation.json") })); return passed ? 0 : 2;
    }
}
