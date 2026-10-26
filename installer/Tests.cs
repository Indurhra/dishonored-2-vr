using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Collections.Generic;
using DvrInstaller;
class Tests {
 static int count;static void Check(bool b,string text){if(!b)throw new Exception(text);Console.WriteLine("PASS "+text);count++;}
 static void Fails(Action a,string text){bool failed=false;try{a();}catch{failed=true;}Check(failed,text);}
 static string Make(string work,string version,string marker){string folder=Path.Combine(work,"payload-"+marker);Directory.CreateDirectory(folder);var entries=new List<Entry>();foreach(var rel in Package.Allowed){string p=Path.Combine(folder,rel.Replace('/','\\'));Directory.CreateDirectory(Path.GetDirectoryName(p));File.WriteAllText(p,rel=="DishonoredVR.ini"?"[Gameplay]\r\nCustomSetting=keep\r\n":marker+rel);entries.Add(new Entry{Path=rel,Sha256=Data.HashFile(p)});}Data.Save(Path.Combine(folder,"manifest.json"),new Manifest{Schema=1,Version=version,Files=entries.ToArray()});string zip=folder+".zip";ZipFile.CreateFromDirectory(folder,zip);return zip;}
 static void Main(string[] args){if(args.Length>0&&args[0]=="--sleep"){System.Threading.Thread.Sleep(30000);return;}string work=args[0];Directory.CreateDirectory(work);string game=Path.Combine(work,"fake-game");Directory.CreateDirectory(game);Func<string,bool> valid=p=>p==game;
  File.WriteAllText(Path.Combine(game,"dinput8.dll"),"previous mod");File.WriteAllText(Path.Combine(game,"DishonoredVR.ini"),"user settings untouched");File.WriteAllText(Path.Combine(game,"save.sav"),"save untouched");
  string p1=Make(work,"0.5.1","one"),p2=Make(work,"0.5.2","two");var engine=new Engine(game,valid,null);
  using(var a=new Package(p1))engine.Install(a,"psvr2");
  Check(engine.ReadState().Version=="0.5.1","fresh install version");Check(File.ReadAllText(Path.Combine(game,"DishonoredVR.ini"))=="user settings untouched","existing settings preserved byte-for-byte");
  Check(File.Exists(Path.Combine(game,"DVR_psvr2","dvr_psvr2_bridge.exe")),"PSVR2 helper included");
  using(var b=new Package(p2))engine.Install(b);
  Check(engine.ReadState().Version=="0.5.2","update version");Check(File.ReadAllText(Path.Combine(game,"DishonoredVR.ini"))=="user settings untouched","update preserves settings");
  engine.Rollback();Check(engine.ReadState().Version=="0.5.1","rollback restores previous version");
  using(var b=new Package(p2)){var failing=new Engine(game,valid,n=>{if(n==2)throw new Exception("injected write failure");});Fails(()=>failing.Install(b),"partial update failure reported");}
  Check(engine.ReadState().Version=="0.5.1"&&File.ReadAllText(Path.Combine(game,"DishonoredVR.dll"))=="oneDishonoredVR.dll","failed update restores files and state");
  var interrupted=new Journal(); foreach(var rel in Package.Allowed.Concat(new[]{"@state"})){string file=rel=="@state"?Path.Combine(game,".dvr-install","state.json"):Path.Combine(game,rel);string hash=File.Exists(file)?Data.HashFile(file):null;if(hash!=null){string blob=Path.Combine(game,".dvr-install","blobs",hash);if(!File.Exists(blob))File.Copy(file,blob);}interrupted.Files.Add(new Snapshot{Path=rel,Hash=hash});} Data.Save(Path.Combine(game,".dvr-install","pending.json"),interrupted);
  File.WriteAllText(Path.Combine(game,"DishonoredVR.dll"),"interrupted partial write");
  using(var b=new Package(p2))engine.Install(b);
  Check(engine.ReadState().Version=="0.5.2"&&!File.Exists(Path.Combine(game,".dvr-install","pending.json")),"next operation recovers interrupted transaction before updating");
  engine.Rollback();
  File.WriteAllText(Path.Combine(game,"DishonoredVR.dll"),"external change");Fails(()=>engine.Uninstall(),"uninstall protects externally modified files");
  File.WriteAllText(Path.Combine(game,"DishonoredVR.dll"),"oneDishonoredVR.dll");engine.Uninstall();
  Check(File.ReadAllText(Path.Combine(game,"dinput8.dll"))=="previous mod","uninstall restores original conflicting proxy");Check(!File.Exists(Path.Combine(game,"DishonoredVR.dll")),"uninstall removes installed DLL");
  Check(File.ReadAllText(Path.Combine(game,"DishonoredVR.ini"))=="user settings untouched"&&File.ReadAllText(Path.Combine(game,"save.sav"))=="save untouched","uninstall preserves settings and saves");
  engine.Uninstall();Check(engine.ReadState()==null,"repeated uninstall is harmless");
  File.Delete(Path.Combine(game,"DishonoredVR.ini"));using(var a=new Package(p1))engine.Install(a,"other");Check(File.ReadAllText(Path.Combine(game,"DishonoredVR.ini")).Contains("Psvr2Triggers = 0"),"fresh other-headset settings disable trigger helper");
  using(var b=new Package(p2))engine.Install(b,"psvr2");Check(File.ReadAllText(Path.Combine(game,"DishonoredVR.ini")).Contains("Psvr2Triggers = 0"),"updates preserve headset INI preference");
  using(var a=new Package(p1))Fails(()=>engine.Install(a),"downgrade refused");
  string bad=Path.Combine(work,"bad.zip");File.Copy(p1,bad);using(var z=ZipFile.Open(bad,ZipArchiveMode.Update)){var e=z.GetEntry("DishonoredVR.dll");e.Delete();using(var w=new StreamWriter(z.CreateEntry("DishonoredVR.dll").Open()))w.Write("corrupt");}Fails(()=>{using(var p=new Package(bad)){}},"corrupt payload rejected");
  string traversal=Path.Combine(work,"traversal.zip");File.Copy(p1,traversal);using(var z=ZipFile.Open(traversal,ZipArchiveMode.Update)){z.GetEntry("DishonoredVR.dll").Delete();z.CreateEntry("../escape.dll");}Fails(()=>{using(var p=new Package(traversal)){}},"archive traversal rejected");
  var release=new Release{tag_name="v0.5.2",assets=new[]{new ReleaseAsset{name="DishonoredVR-Payload-0.5.2.zip",browser_download_url="https://github.com/Indurhra/dishonored-2-vr/releases/download/v0.5.2/DishonoredVR-Payload-0.5.2.zip",size=new FileInfo(p2).Length,digest="sha256:"+Data.HashFile(p2)}}};
  var selected=Updates.Select(release,"0.5.1");Updates.Verify(selected,p2);Check(selected!=null,"new stable release selected and digest verified");Check(Updates.Select(release,"0.5.2")==null,"same version skipped");release.prerelease=true;Check(Updates.Select(release,"0.5.1")==null,"prereleases skipped");release.prerelease=false;
  engine.Rollback();Check(engine.ReadState().Version=="0.5.1","prepare prior version for updater test");
  Fails(()=>Updates.ApplyRelease(engine,release,(a,p)=>{throw new IOException("offline");}),"offline update fails without changes");Check(engine.ReadState().Version=="0.5.1","offline failure preserves installed version");
  Check(Updates.ApplyRelease(engine,release,(a,p)=>File.Copy(p2,p)),"automatic update pipeline applies verified new package");
  Check(engine.ReadState().Version=="0.5.2"&&File.ReadAllText(Path.Combine(game,"DishonoredVR.ini")).Contains("Psvr2Triggers = 0"),"automatic update preserves settings and advances version");
  string sleeper=Path.Combine(work,"Dishonored2.exe");File.Copy(System.Reflection.Assembly.GetExecutingAssembly().Location,sleeper);
  using(var process=System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(sleeper,"--sleep"){UseShellExecute=false,CreateNoWindow=true})) {
   try {System.Threading.Thread.Sleep(100);Fails(()=>engine.Uninstall(),"running game blocks uninstall");using(var b=new Package(p2))Fails(()=>engine.Install(b),"running game blocks update");}
   finally{if(!process.HasExited)process.Kill();process.WaitForExit();}
  }
  selected.digest="sha256:"+new string('0',64);Fails(()=>Updates.Verify(selected,p2),"download hash mismatch rejected");selected.browser_download_url="https://example.org/update.zip";Fails(()=>Updates.Select(release,"0.5.1"),"foreign update URL rejected");
  string steam=Path.Combine(work,"steam"),library=Path.Combine(work,"other library");Directory.CreateDirectory(Path.Combine(steam,"steamapps"));File.WriteAllText(Path.Combine(steam,"steamapps","libraryfolders.vdf"),"\"libraryfolders\" { \"0\" { \"path\" \""+library.Replace("\\","\\\\")+"\" } }");Check(Discovery.Libraries(steam).Contains(library),"custom Steam library detected");
  Check(!Discovery.Supported(game),"unsupported executable rejected");Console.WriteLine("ALL "+count+" CHECKS PASSED");
 }
}


