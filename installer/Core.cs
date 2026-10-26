using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Collections.Generic;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;
using Microsoft.Win32;

namespace DvrInstaller {
 public class Entry { public string Path; public string Sha256; }
 public class Manifest { public int Schema; public string Version; public Entry[] Files; }
 public class Owned { public string Path; public string InstalledHash; public string OriginalHash; }
 public class State { public int Schema=1; public string Version; public List<Owned> Files=new List<Owned>(); }
 public class Snapshot { public string Path; public string Hash; }
 public class Journal { public List<Snapshot> Files=new List<Snapshot>(); }
 public static class Data {
  public static JavaScriptSerializer Json=new JavaScriptSerializer { MaxJsonLength=1024*1024 };
  public static string Hash(byte[] b) { using(var s=SHA256.Create())return BitConverter.ToString(s.ComputeHash(b)).Replace("-","").ToLowerInvariant(); }
  public static string HashFile(string p) { using(var s=SHA256.Create())using(var f=File.OpenRead(p))return BitConverter.ToString(s.ComputeHash(f)).Replace("-","").ToLowerInvariant(); }
  public static void Save(string p,object v) { Atomic(p,System.Text.Encoding.UTF8.GetBytes(Json.Serialize(v))); }
  public static T Load<T>(string p) { return Json.Deserialize<T>(File.ReadAllText(p)); }
  public static void Atomic(string p,byte[] b) {
   Directory.CreateDirectory(System.IO.Path.GetDirectoryName(p));
   string temp=p+".new-"+Guid.NewGuid().ToString("N");
   try {File.WriteAllBytes(temp,b); if(File.Exists(p))File.Replace(temp,p,null);else File.Move(temp,p);}
   finally {if(File.Exists(temp))File.Delete(temp);}
  }
 }
 public static class Discovery {
  public static bool ToolkitFound() {
   try {
    string p=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"openvr","openvrpaths.vrpath");
    if(!File.Exists(p))return false;
    var paths=Data.Json.Deserialize<Dictionary<string,object>>(File.ReadAllText(p));
    object list;if(!paths.TryGetValue("external_drivers",out list))return false;
    foreach(var driver in (System.Collections.IEnumerable)list)
     if(File.Exists(Path.Combine(Convert.ToString(driver),"bin","win64","psvr2_toolkit_capi.dll")))return true;
   }catch{}
   return false;
  }
  public static bool Supported(string root) {
   try {
    var p=Path.Combine(root,"Dishonored2.exe");
    if(new FileInfo(p).Length!=41134632 || FileVersionInfo.GetVersionInfo(p).FileVersion!="1.77.9.0")return false;
    using(var r=new BinaryReader(File.OpenRead(p))) {r.BaseStream.Position=0x3c;int pe=r.ReadInt32();r.BaseStream.Position=pe;return r.ReadUInt32()==0x4550 && (r.ReadUInt32()&0xffff)==0x8664 && r.ReadUInt32()==0x67A50CDE;}
   }catch{return false;}
  }
  public static List<string> Libraries(string steam) {
   var roots=new HashSet<string>(StringComparer.OrdinalIgnoreCase);roots.Add(steam);
   var file=Path.Combine(steam,"steamapps","libraryfolders.vdf");
   if(File.Exists(file))foreach(Match m in Regex.Matches(File.ReadAllText(file),"\"(?:path|[0-9]+)\"\\s*\"([^\"]+)\"")) {
    string v=m.Groups[1].Value.Replace("\\\\","\\");if(Path.IsPathRooted(v))roots.Add(v);
   }
   return roots.ToList();
  }
  public static List<string> Find() {
   var roots=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   foreach(var pair in new[]{new[]{"HKEY_CURRENT_USER\\Software\\Valve\\Steam","SteamPath"},new[]{"HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam","InstallPath"}}) {
    var value=Registry.GetValue(pair[0],pair[1],null) as string;if(!String.IsNullOrEmpty(value))roots.Add(value);
   }
   var found=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   foreach(var steam in roots)foreach(var lib in Libraries(steam)) {
    string apps=Path.Combine(lib,"steamapps"), folder="Dishonored2", manifest=Path.Combine(apps,"appmanifest_403640.acf");
    if(File.Exists(manifest)) {var m=Regex.Match(File.ReadAllText(manifest),"\"installdir\"\\s*\"([^\"]+)\"");if(m.Success)folder=m.Groups[1].Value;}
    if(folder.IndexOfAny(new[]{'\\','/',':'})>=0 || folder=="..")continue;
    string game=Path.Combine(apps,"common",folder);if(File.Exists(Path.Combine(game,"Dishonored2.exe")))found.Add(game);
   }
   return found.ToList();
  }
 }
 public class Package : IDisposable {
  public Manifest Manifest; public Dictionary<string,byte[]> Files=new Dictionary<string,byte[]>(StringComparer.OrdinalIgnoreCase);
  public static readonly HashSet<string> Allowed=new HashSet<string>(new[]{"dinput8.dll","DishonoredVR.dll","DishonoredVR.ini","DVR_psvr2/dvr_psvr2_bridge.exe","DVR_psvr2/psvr2_toolkit_capi_loader.dll","DVR_psvr2/PSVR2Toolkit-LICENSE.txt","DVR_Docs/LICENSE.txt","DVR_Docs/THIRD-PARTY-NOTICES.txt","DVR_Docs/README.txt"},StringComparer.OrdinalIgnoreCase);
  public Package(string path) {
   using(var zip=ZipFile.OpenRead(path)) {
    if(zip.Entries.Count!=Allowed.Count+1)throw new Exception("Package has missing or unexpected files.");
    var names=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
    foreach(var e in zip.Entries) {
     string name=e.FullName.Replace('\\','/');
     if(!names.Add(name) || (name!="manifest.json" && !Allowed.Contains(name)) || e.Length>64*1024*1024)throw new Exception("Invalid package entry: "+name);
     using(var stream=e.Open())using(var memory=new MemoryStream()){stream.CopyTo(memory);Files[name]=memory.ToArray();}
    }
   }
   if(!Files.ContainsKey("manifest.json"))throw new Exception("Missing manifest.");
   Manifest=Data.Json.Deserialize<Manifest>(System.Text.Encoding.UTF8.GetString(Files["manifest.json"]));Files.Remove("manifest.json");
   Version parsed;
   if(Manifest.Schema!=1 || !Version.TryParse(Manifest.Version,out parsed) || Manifest.Files==null || Manifest.Files.Length!=Allowed.Count)throw new Exception("Unsupported package version/schema.");
   var checkedNames=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   foreach(var e in Manifest.Files)if(e==null || !checkedNames.Add(e.Path) || !Allowed.Contains(e.Path) || !Files.ContainsKey(e.Path) || Data.Hash(Files[e.Path])!=e.Sha256)throw new Exception("Package integrity check failed.");
  }
  public void Dispose(){}
 }
 public class Engine {
  readonly string root, meta; readonly Func<string,bool> compatible; readonly Action<int> fault;
  public Engine(string game):this(game,Discovery.Supported,null){}
  // Injected compatibility and fault hooks are used by the separate test executable only.
  public Engine(string game,Func<string,bool> check,Action<int> injection) {
   root=Path.GetFullPath(game).TrimEnd('\\');meta=Path.Combine(root,".dvr-install");compatible=check;fault=injection;
   if(root==Path.GetPathRoot(root).TrimEnd('\\'))throw new Exception("Choose the game folder, not a drive root.");
   Safe(root);Safe(meta);
  }
  public string Root {get{return root;}}
  static void Safe(string p) {
   for(string d=Path.GetFullPath(p);!String.IsNullOrEmpty(d);d=Path.GetDirectoryName(d))
    if((File.Exists(d)||Directory.Exists(d)) && (File.GetAttributes(d)&FileAttributes.ReparsePoint)!=0)throw new Exception("Linked folders/files are not supported: "+d);
  }
  string Target(string rel) {if(!Package.Allowed.Contains(rel))throw new Exception("Unexpected managed path.");var p=Path.Combine(root,rel.Replace('/','\\'));Safe(p);return p;}
  string Internal(string rel) {var p=Path.Combine(meta,rel);Safe(p);return p;}
  string Blob(string hash) {if(!Regex.IsMatch(hash??"","^[a-f0-9]{64}$"))throw new Exception("Invalid backup hash.");return Internal("blobs\\"+hash);}
  void Backup(string p,string hash) {var blob=Blob(hash);if(!File.Exists(blob))Data.Atomic(blob,File.ReadAllBytes(p));if(Data.HashFile(blob)!=hash)throw new Exception("Backup checksum mismatch.");}
  public void Check() {
   if(!compatible(root))throw new Exception("Supported Steam Dishonored 2 build 1.77.9.0 was not found here.");
   if(Process.GetProcessesByName("Dishonored2").Length>0 || Process.GetProcessesByName("dvr_psvr2_bridge").Length>0)throw new Exception("Close Dishonored 2 before installing, updating or removing the mod.");
  }
  public State ReadState() {
   string p=Internal("state.json");if(!File.Exists(p))return null;
   var s=Data.Load<State>(p);Version v;
   if(s.Schema!=1 || !Version.TryParse(s.Version,out v) || s.Files==null)throw new Exception("Invalid installation record.");
   var unique=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   foreach(var f in s.Files){Target(f.Path);if(!unique.Add(f.Path)||!Regex.IsMatch(f.InstalledHash??"","^[a-f0-9]{64}$"))throw new Exception("Invalid file record.");if(f.OriginalHash!=null)Blob(f.OriginalHash);}
   return s;
  }
  FileStream Lock() {Directory.CreateDirectory(meta);return new FileStream(Internal("lock"),FileMode.OpenOrCreate,FileAccess.ReadWrite,FileShare.None);}
  void Restore(Journal j) {
   foreach(var s in j.Files){string p=s.Path=="@state"?Internal("state.json"):Target(s.Path);if(s.Hash==null){if(File.Exists(p))File.Delete(p);}else{string b=Blob(s.Hash);if(Data.HashFile(b)!=s.Hash)throw new Exception("Cannot restore: damaged backup.");Data.Atomic(p,File.ReadAllBytes(b));}}
  }
  void Recover() {var p=Internal("pending.json");if(File.Exists(p)){Restore(Data.Load<Journal>(p));File.Delete(p);}}
  void Transaction(Action action) {
   var j=new Journal();
   foreach(var rel in Package.Allowed.Concat(new[]{"@state"})) {string p=rel=="@state"?Internal("state.json"):Target(rel);string hash=File.Exists(p)?Data.HashFile(p):null;if(hash!=null)Backup(p,hash);j.Files.Add(new Snapshot{Path=rel,Hash=hash});}
   Data.Save(Internal("pending.json"),j);
   try {action();Data.Save(Internal("rollback.json"),j);File.Delete(Internal("pending.json"));}
   catch {Restore(j);File.Delete(Internal("pending.json"));throw;}
  }
  public void Install(Package package,string headset="auto") {
   Check();using(Lock()){Recover();var before=ReadState();
    if(before!=null && new Version(package.Manifest.Version)<new Version(before.Version))throw new Exception("Older package refused. Use Roll back instead.");
    foreach(var old in before==null?new List<Owned>():before.Files)if(old.Path!="DishonoredVR.ini") {var path=Target(old.Path);if(File.Exists(path)&&Data.HashFile(path)!=old.InstalledHash)throw new Exception("Managed file changed outside this installer: "+old.Path+". Preserve it before updating.");}
    Transaction(()=>{var after=new State{Version=package.Manifest.Version};int n=0;
     foreach(var entry in package.Manifest.Files) {
      var p=Target(entry.Path);var prior=before==null?null:before.Files.Find(x=>x.Path.Equals(entry.Path,StringComparison.OrdinalIgnoreCase));
      if(entry.Path=="DishonoredVR.ini" && File.Exists(p)){if(prior!=null)after.Files.Add(prior);continue;}
      string original=prior==null?(File.Exists(p)?Data.HashFile(p):null):prior.OriginalHash;
      if(prior==null&&original!=null)Backup(p,original);
      byte[] content=package.Files[entry.Path];
      if(entry.Path=="DishonoredVR.ini")content=System.Text.Encoding.UTF8.GetBytes(System.Text.Encoding.UTF8.GetString(content).Replace("[Gameplay]","[Gameplay]\r\n; Installer headset preference (runtime still verifies PSVR2).\r\nPsvr2Triggers = "+(headset=="other"?"0":"1")));
      Data.Atomic(p,content);after.Files.Add(new Owned{Path=entry.Path,InstalledHash=Data.Hash(content),OriginalHash=original});
      if(fault!=null)fault(++n);
     }
     Data.Save(Internal("state.json"),after);
    });
   }
  }
  public void Uninstall() {
   Check();using(Lock()){Recover();var before=ReadState();if(before==null)return;
    foreach(var f in before.Files)if(f.Path!="DishonoredVR.ini" && File.Exists(Target(f.Path)) && Data.HashFile(Target(f.Path))!=f.InstalledHash)throw new Exception("Uninstall stopped to preserve externally modified file: "+f.Path);
    Transaction(()=>{foreach(var f in before.Files){if(f.Path=="DishonoredVR.ini")continue;var p=Target(f.Path);if(f.OriginalHash!=null){var b=Blob(f.OriginalHash);if(Data.HashFile(b)!=f.OriginalHash)throw new Exception("Damaged original backup.");Data.Atomic(p,File.ReadAllBytes(b));}else if(File.Exists(p))File.Delete(p);}File.Delete(Internal("state.json"));});
   }
  }
  public void Rollback() {
   Check();using(Lock()){Recover();var state=ReadState();
    if(state!=null)foreach(var f in state.Files)if(f.Path!="DishonoredVR.ini" && File.Exists(Target(f.Path))&&Data.HashFile(Target(f.Path))!=f.InstalledHash)throw new Exception("Modified file blocks rollback: "+f.Path);
    var p=Internal("rollback.json");if(!File.Exists(p))throw new Exception("No previous installation to restore.");var j=Data.Load<Journal>(p);
    j.Files.RemoveAll(x=>x.Path=="DishonoredVR.ini");Transaction(()=>Restore(j));
   }
  }
 }
}

