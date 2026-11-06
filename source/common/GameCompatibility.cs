using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Diagnostics;
using System.Globalization;
using System.Text.RegularExpressions;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Web.Script.Serialization;

// File-only compatibility assessment. Never loads a client DLL, opens a game
// process, launches an executable or writes configuration/status files.
public static class GameCompatibility {
    public sealed class FileRule { public string path, sha256; public long bytes; }
    public sealed class Profile { public string id, label; public FileRule[] samples; }
    public sealed class AnchorSet { public string id; public FileRule[] files; }
    public sealed class BundleRule { public string name, guid, sha256; public long bytes; }
    public sealed class ManifestRule { public string directory, fileNameRegex, packageName, packageVersionRegex, bundleFilesRoot; public BundleRule[] requiredBundles; }
    public sealed class Policy { public int version; public string appInfoPath, companyName, productName; public string[] supportedUnityBranches, requiredPe64Files, requiredFiles; public AnchorSet[] renderAnchorSets; public ManifestRule manifest; }
    public sealed class Package { public FileRule[] gameSamples; public Profile[] knownProfiles; public Policy compatibilityPolicy; }
    public sealed class Check { public string id, path, expected, actual, detail; public bool passed, required; }
    public sealed class Result {
        public int version = 1; public bool allowed; public string state = "unsupported", profileId, message, unityProductVersion, selectedManifest, selectedPackageVersion;
        public string manifestAssurance = "newest-cached-manifest-not-proof-of-online-runtime-version";
        public List<Check> checks = new List<Check>();
    }
    sealed class Bundle { public string name,guid; public long bytes; public int[] dependencies; }
    sealed class CacheManifest { public string file,packageVersion; public long version,cacheNumber; public List<Bundle> bundles; }
    static readonly UTF8Encoding Utf8 = new UTF8Encoding(false, true);
    static readonly JavaScriptSerializer Json = new JavaScriptSerializer { MaxJsonLength = 2097152 };
    static bool Sha(string s) { return s != null && Regex.IsMatch(s,"^[0-9a-f]{64}$"); }
    static string At(string root,string relative) {
        if(String.IsNullOrWhiteSpace(relative)||Path.IsPathRooted(relative)||relative.IndexOf(':')>=0)throw new Exception("兼容策略含无效相对路径。");
        string prefix=Path.GetFullPath(root).TrimEnd('\\')+"\\",path=Path.GetFullPath(Path.Combine(prefix,relative));
        if(!path.StartsWith(prefix,StringComparison.OrdinalIgnoreCase))throw new Exception("兼容策略路径越界。");return path;
    }
    static string Hash(string path,Dictionary<string,string> cache) {
        string value;if(cache.TryGetValue(path,out value))return value;
        using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete))using(var sha=SHA256.Create())value=BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","").ToLowerInvariant();cache[path]=value;return value;
    }
    static void Add(Result r,string id,string path,string expected,string actual,bool pass,bool required,string detail) { r.checks.Add(new Check{id=id,path=path,expected=expected,actual=actual,passed=pass,required=required,detail=detail}); }
    static Regex Pattern(string value) { if(String.IsNullOrEmpty(value)||!value.StartsWith("^")||!value.EndsWith("$"))throw new Exception("Manifest匹配表达式必须完整限定。");return new Regex(value,RegexOptions.CultureInvariant,TimeSpan.FromMilliseconds(250)); }
    static void ValidateRule(string root,FileRule f) { if(f==null||!Sha(f.sha256)||f.bytes<0)throw new Exception("兼容文件规则无效。");At(root,f.path); }
    static void ValidatePolicy(string root,Policy p) {
        if(p==null||p.version!=1||String.IsNullOrWhiteSpace(p.companyName)||String.IsNullOrWhiteSpace(p.productName))throw new Exception("缺少有效的客户端兼容策略。");
        At(root,p.appInfoPath);
        if(p.supportedUnityBranches==null||p.supportedUnityBranches.Length==0||p.supportedUnityBranches.Any(s=>s==null||!Regex.IsMatch(s,"^[0-9]{4}\\.[0-9]+$")))throw new Exception("Unity支持分支配置无效。");
        if(p.requiredPe64Files==null||p.requiredFiles==null||!new[]{"Lumin.exe","UnityPlayer.dll","GameAssembly.dll"}.All(n=>p.requiredPe64Files.Contains(n,StringComparer.OrdinalIgnoreCase)))throw new Exception("缺少必要的64位客户端身份规则。");
        foreach(string s in p.requiredPe64Files.Concat(p.requiredFiles))At(root,s);
        if(p.renderAnchorSets==null||p.renderAnchorSets.Length==0)throw new Exception("缺少渲染来源签名组。");
        foreach(var group in p.renderAnchorSets){if(group==null||String.IsNullOrEmpty(group.id)||group.files==null||group.files.Length==0)throw new Exception("渲染签名组为空。");foreach(var f in group.files)ValidateRule(root,f);}
        var m=p.manifest;if(m==null||String.IsNullOrEmpty(m.packageName)||m.requiredBundles==null||m.requiredBundles.Length==0)throw new Exception("缺少最新缓存Manifest规则。");
        At(root,m.directory);At(root,m.bundleFilesRoot);Pattern(m.fileNameRegex);Pattern(m.packageVersionRegex);
        foreach(var b in m.requiredBundles)if(b==null||String.IsNullOrEmpty(b.name)||b.guid==null||!Regex.IsMatch(b.guid,"^[a-f0-9]{32}$")||b.bytes<=0||!Sha(b.sha256))throw new Exception("Manifest渲染包规则无效。");
    }
    static string Pe64(string path) {
        using(var f=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete))using(var b=new BinaryReader(f)) {
            if(f.Length<256||b.ReadUInt16()!=0x5a4d)return "缺少有效MZ头";f.Position=0x3c;uint offset=b.ReadUInt32();
            if(offset<64||offset>f.Length-26)return "PE头偏移无效";f.Position=offset;if(b.ReadUInt32()!=0x00004550)return "缺少PE签名";
            ushort machine=b.ReadUInt16();f.Position=offset+20;ushort size=b.ReadUInt16();if(size<2||offset+24L+size>f.Length)return "PE可选头长度无效";
            f.Position=offset+24;ushort magic=b.ReadUInt16();return machine==0x8664&&magic==0x20b?null:"需要AMD64 PE32+，实际machine="+machine.ToString("x4")+"/magic="+magic.ToString("x4");
        }
    }
    static bool FileMatches(string root,FileRule rule,Dictionary<string,string> cache,out string actual) {
        string path=At(root,rule.path);if(!File.Exists(path)){actual="文件不存在";return false;}if(rule.bytes>0&&new FileInfo(path).Length!=rule.bytes){actual="长度="+new FileInfo(path).Length;return false;}actual=Hash(path,cache);return actual==rule.sha256;
    }
    public static Result Inspect(string runtimeRoot,string gameExecutable) {
        var result=new Result();var cache=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
        try {
            string file=Path.Combine(runtimeRoot,"package.json");if(!File.Exists(file)||new FileInfo(file).Length>2097152)throw new Exception("兼容清单缺失或过大。");
            var package=Json.Deserialize<Package>(File.ReadAllText(file,Utf8));if(package==null)throw new Exception("兼容清单无效。");
            if(String.IsNullOrWhiteSpace(gameExecutable)||!Path.IsPathRooted(gameExecutable)||!Path.GetFileName(gameExecutable).Equals("Lumin.exe",StringComparison.OrdinalIgnoreCase))throw new Exception("请选择游戏本体Lumin.exe。");
            string root=Path.GetDirectoryName(Path.GetFullPath(gameExecutable));var policy=package.compatibilityPolicy;ValidatePolicy(root,policy);
            foreach(string name in policy.requiredPe64Files){string path=At(root,name),error=File.Exists(path)?Pe64(path):"文件不存在";Add(result,"pe64",name,"AMD64 PE32+",error??"AMD64 PE32+",error==null,true,error);}
            foreach(string name in policy.requiredFiles)Add(result,"required-file",name,"存在",File.Exists(At(root,name))?"存在":"缺失",File.Exists(At(root,name)),true,null);
            string app=At(root,policy.appInfoPath);bool identity=false;string text="文件不存在";
            if(File.Exists(app)&&new FileInfo(app).Length<=4096){text=File.ReadAllText(app,Utf8).Replace("\r\n","\n").TrimEnd('\n');identity=text==policy.companyName+"\n"+policy.productName;}
            Add(result,"game-identity",policy.appInfoPath,policy.companyName+"/"+policy.productName,text.Replace('\n','/'),identity,true,null);
            string unity=At(root,"UnityPlayer.dll");result.unityProductVersion=File.Exists(unity)?FileVersionInfo.GetVersionInfo(unity).ProductVersion:null;
            var branch=Regex.Match(result.unityProductVersion??"","^([0-9]{4}\\.[0-9]+)\\.");bool supported=branch.Success&&policy.supportedUnityBranches.Contains(branch.Groups[1].Value,StringComparer.Ordinal);
            Add(result,"unity-branch","UnityPlayer.dll",String.Join(",",policy.supportedUnityBranches),result.unityProductVersion,supported,true,null);
            if(result.checks.Any(c=>c.required&&!c.passed))return Finish(result);
            var profiles=new List<Profile>();if(package.knownProfiles!=null)profiles.AddRange(package.knownProfiles);if(package.gameSamples!=null&&package.gameSamples.Length>0)profiles.Add(new Profile{id="legacy-pinned",label="原固定版本",samples=package.gameSamples});
            Profile known=null;
            foreach(var profile in profiles){if(profile==null||String.IsNullOrEmpty(profile.id)||profile.samples==null||profile.samples.Length==0)throw new Exception("已知客户端版本组无效。");bool matches=true;
                foreach(var f in profile.samples){ValidateRule(root,f);string actual;bool ok=FileMatches(root,f,cache,out actual);Add(result,"known-profile:"+profile.id,f.path,f.sha256,actual,ok,false,null);matches&=ok;}
                if(matches){known=profile;break;}
            }
            bool anchors=false;
            foreach(var group in policy.renderAnchorSets){bool match=true;foreach(var f in group.files){string actual;bool ok=FileMatches(root,f,cache,out actual);Add(result,"render-anchor:"+group.id,f.path,f.sha256,actual,ok,false,null);match&=ok;}if(match){anchors=true;result.profileId=group.id;break;}}
            Add(result,"render-anchor-set",null,"至少一个完整渲染签名组",anchors?result.profileId:"无匹配组",anchors,true,null);
            if(!anchors)return Finish(result);
            CheckManifest(root,policy.manifest,result,cache);
            if(!result.checks.Any(c=>c.required&&!c.passed)){result.allowed=true;result.state=known==null?"compatible-update":"known-profile";if(known!=null)result.profileId=known.id;result.message=known==null?"客户端已更新；游戏身份、Unity分支、渲染来源和最新缓存Manifest检查通过。运行时仍逐资源匹配，需确认本次画面。":"已识别客户端版本，游戏身份、64位架构、Unity分支及最新缓存渲染来源检查通过。";return result;}
            return Finish(result);
        }catch(Exception error){Add(result,"assessment",null,"完整且可验证的兼容策略",error.Message,false,true,null);return Finish(result);}
    }
    static Result Finish(Result r){r.allowed=false;r.state="unsupported";r.message="客户端兼容检查未通过："+String.Join("；",r.checks.Where(c=>c.required&&!c.passed).Select(c=>c.path==null?c.actual:c.path+"："+c.actual));return r;}
    public static Result Validate(string runtimeRoot,string gameExecutable){var r=Inspect(runtimeRoot,gameExecutable);if(!r.allowed)throw new Exception(r.message);return r;}
    public static int RunReadOnlyCli(string runtimeRoot,string gameExecutable){
        var r=Inspect(runtimeRoot,gameExecutable);
        try{using(var output=Console.OpenStandardOutput())using(var writer=new StreamWriter(output,new UTF8Encoding(false))){writer.WriteLine(Json.Serialize(r));writer.Flush();}}
        catch(IOException e){if((e.HResult&65535)!=6)throw;} // A GUI-subsystem process may have no inherited stdout.
        return r.allowed?0:2;
    }

    sealed class Reader:IDisposable {
        readonly BinaryReader b;readonly Stream stream;
        public Reader(byte[]data):this(new MemoryStream(data,false)){}
        public Reader(Stream input){stream=input;b=new BinaryReader(stream,Utf8);}
        public uint U32(){return b.ReadUInt32();}public int I32(){return b.ReadInt32();}public long I64(){return b.ReadInt64();}public byte Byte(){return b.ReadByte();}
        public string S(){int n=b.ReadUInt16();var bytes=b.ReadBytes(n);if(bytes.Length!=n)throw new Exception("Manifest字符串截断。");return Utf8.GetString(bytes);}
        public string[] SA(){int n=b.ReadUInt16();var a=new string[n];for(int i=0;i<n;i++)a[i]=S();return a;}
        public int[] IA(){int n=b.ReadUInt16();var a=new int[n];for(int i=0;i<n;i++)a[i]=I32();return a;}
        public bool End {get{return stream.Position==stream.Length;}}public void Dispose(){b.Dispose();}
    }
    static uint Crc(byte[]bytes){uint crc=0xffffffff;foreach(byte item in bytes){crc^=item;for(int bit=0;bit<8;bit++)crc=(crc>>1)^((crc&1)!=0?0xedb88320:0);}return ~crc;}
    static CacheManifest ReadHeader(Reader r,string file,ManifestRule rule,long cacheNumber) {
        if(r.U32()!=0x00594f4f||r.S()!="2.3.1")throw new Exception("未知Manifest格式："+Path.GetFileName(file));
        for(int i=0;i<3;i++)if(r.Byte()>1)throw new Exception("Manifest布尔字段无效："+Path.GetFileName(file));r.I32();r.I32();r.S();string package=r.S(),version=r.S();r.S();
        var m=Pattern(rule.packageVersionRegex).Match(version);long number;if(package!=rule.packageName||!m.Success||m.Groups.Count<2||!Int64.TryParse(m.Groups[1].Value,out number))throw new Exception("未知Manifest包或版本："+Path.GetFileName(file));
        return new CacheManifest{file=file,packageVersion=version,version=number,cacheNumber=cacheNumber};
    }
    static CacheManifest PeekManifest(string file,ManifestRule rule,long cacheNumber) {
        if(new FileInfo(file).Length<32||new FileInfo(file).Length>64*1024*1024)throw new Exception("无法确定Manifest候选版本："+Path.GetFileName(file));
        try{using(var r=new Reader(new FileStream(file,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete)))return ReadHeader(r,file,rule,cacheNumber);}
        catch(Exception e){throw new Exception("无法确定Manifest候选版本："+Path.GetFileName(file)+"（"+e.Message+"）");}
    }
    static CacheManifest ParseManifest(string file,ManifestRule rule,long cacheNumber) {
        var info=new FileInfo(file);if(info.Length<32||info.Length>64*1024*1024)throw new Exception("Manifest大小无效："+info.Name);
        byte[]data=File.ReadAllBytes(file);string side=Path.ChangeExtension(file,".hash");if(!File.Exists(side)||new FileInfo(side).Length>64)throw new Exception("缺少Manifest校验文件："+Path.GetFileName(side));
        string expected=File.ReadAllText(side,Utf8).Trim().ToLowerInvariant(),actual=BitConverter.ToString(BitConverter.GetBytes(Crc(data))).Replace("-","").ToLowerInvariant();if(!Regex.IsMatch(expected,"^[0-9a-f]{8}$")||expected!=actual)throw new Exception("Manifest CRC不一致："+info.Name);
        using(var r=new Reader(data)) {
            var header=ReadHeader(r,file,rule,cacheNumber);
            int assets=r.I32();if(assets<1||assets>99999)throw new Exception("Manifest asset数量无效。");var references=new List<int>();
            for(int i=0;i<assets;i++){r.S();r.S();r.S();r.SA();references.Add(r.I32());references.AddRange(r.IA());}
            int count=r.I32();if(count<1||count>99999)throw new Exception("Manifest bundle数量无效。");var bundles=new List<Bundle>();var names=new HashSet<string>(StringComparer.Ordinal);
            for(int i=0;i<count;i++){string name=r.S();r.U32();string guid=r.S();r.S();long size=r.I64();byte encrypted=r.Byte();r.SA();r.S();int[]deps=r.IA();if(!names.Add(name)||size<0||encrypted>1||!Regex.IsMatch(guid,"^[a-f0-9]{32}$"))throw new Exception("Manifest bundle条目无效。");references.AddRange(deps);bundles.Add(new Bundle{name=name,guid=guid,bytes=size,dependencies=deps});}
            if(!r.End||references.Any(n=>n<0||n>=count))throw new Exception("Manifest尾部或依赖索引无效。");
            header.bundles=bundles;return header;
        }
    }
    static void CheckManifest(string root,ManifestRule rule,Result result,Dictionary<string,string>cache) {
        string dir=At(root,rule.directory);if(!Directory.Exists(dir))throw new Exception("缺少Manifest缓存目录。");var pattern=Pattern(rule.fileNameRegex);var manifests=new List<CacheManifest>();
        foreach(string file in Directory.GetFiles(dir,"*.bytes")){var m=pattern.Match(Path.GetFileName(file));if(!m.Success){if(Path.GetFileName(file).StartsWith(rule.packageName+"_",StringComparison.OrdinalIgnoreCase))throw new Exception("未知Manifest版本族："+Path.GetFileName(file));continue;}long sequence;if(m.Groups.Count<2||!Int64.TryParse(m.Groups[1].Value,out sequence))throw new Exception("Manifest缓存序号无效。");if(manifests.Count>=128)throw new Exception("Manifest候选过多。");manifests.Add(PeekManifest(file,rule,sequence));}
        if(manifests.Count==0)throw new Exception("未找到可验证的Manifest缓存候选。");var newest=manifests.OrderByDescending(m=>m.version).ThenByDescending(m=>m.cacheNumber).First();
        if(manifests.Count(m=>m.version==newest.version&&m.cacheNumber==newest.cacheNumber)>1)throw new Exception("最新Manifest候选版本存在歧义。");
        CacheManifest selected;try{selected=ParseManifest(newest.file,rule,newest.cacheNumber);}catch(Exception e){throw new Exception("最新Manifest验证失败："+Path.GetFileName(newest.file)+"（"+e.Message+"）");}if(selected.version!=newest.version)throw new Exception("Manifest选择期间发生更新，请重新检查。");result.selectedManifest=Path.GetFileName(selected.file);result.selectedPackageVersion=selected.packageVersion;
        Add(result,"manifest-selection",rule.directory,"最高头内包版本（非mtime）",selected.packageVersion+" / "+result.selectedManifest,true,true,"仅能证明最新本地缓存，不声称在线活动内存版本。");
        foreach(var expected in rule.requiredBundles){var found=selected.bundles.Where(b=>b.name==expected.name).ToArray();bool match=found.Length==1&&found[0].guid==expected.guid&&found[0].bytes==expected.bytes;
            Add(result,"manifest-bundle",expected.name,expected.guid+" / "+expected.bytes,found.Length==1?found[0].guid+" / "+found[0].bytes:"缺少或重复",match,true,null);
            if(match){string relative=rule.bundleFilesRoot.TrimEnd('\\','/')+"/"+expected.guid.Substring(0,2)+"/"+expected.guid+"/__data";string actual;bool valid=FileMatches(root,new FileRule{path=relative,sha256=expected.sha256,bytes=expected.bytes},cache,out actual);Add(result,"manifest-bundle-file",relative,expected.sha256,actual,valid,true,null);}
        }
    }
}
