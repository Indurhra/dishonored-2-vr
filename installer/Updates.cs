using System;
using System.IO;
using System.Net;
using System.Linq;
using System.Collections.Generic;
using System.Text.RegularExpressions;

namespace DvrInstaller {
 public class ReleaseAsset {public string name; public string browser_download_url; public string digest; public long size;}
 public class Release {public string tag_name; public bool draft; public bool prerelease; public ReleaseAsset[] assets;}
 public static class Updates {
  public const string Repository="Indurhra/dishonored-2-vr";
  public const string Latest="https://api.github.com/repos/"+Repository+"/releases/latest";
  public static ReleaseAsset Select(Release release,string current) {
   if(release==null || release.draft || release.prerelease)return null;
   Version version,installed;
   string tag=release.tag_name??"";
   if(!tag.StartsWith("v") || !Version.TryParse(tag.Substring(1),out version) || !Version.TryParse(current,out installed))return null;
   if(version<=installed)return null;
   string expected="DishonoredVR-Payload-"+version+".zip";
   var matches=(release.assets??new ReleaseAsset[0]).Where(a=>a.name==expected).ToArray();
   if(matches.Length!=1)throw new Exception("This release needs a newer installer or is missing its update package.");
   var asset=matches[0];
   string url="https://github.com/"+Repository+"/releases/download/"+tag+"/"+expected;
   if(asset.browser_download_url!=url || asset.size<=0 || asset.size>128*1024*1024 || !Regex.IsMatch(asset.digest??"","^sha256:[a-f0-9]{64}$"))throw new Exception("Update metadata failed validation. Existing installation was left alone.");
   return asset;
  }
  public static void Verify(ReleaseAsset asset,string path) {
   if(new FileInfo(path).Length!=asset.size || "sha256:"+Data.HashFile(path)!=asset.digest)throw new Exception("Downloaded update checksum/size failed. Existing installation was left alone.");
  }
  static bool Allowed(Uri u) {
   return u.Scheme=="https" && u.IsDefaultPort && String.IsNullOrEmpty(u.UserInfo) &&
    (u.Host=="api.github.com" || u.Host=="github.com" || u.Host=="release-assets.githubusercontent.com" || u.Host=="objects.githubusercontent.com");
  }
  static HttpWebResponse Open(string url) {
   ServicePointManager.SecurityProtocol=SecurityProtocolType.Tls12;
   Uri uri=new Uri(url);
   for(int redirect=0;redirect<6;redirect++) {
    if(!Allowed(uri))throw new Exception("Unexpected update download destination.");
    var request=(HttpWebRequest)WebRequest.Create(uri);request.UserAgent="DishonoredVR-Installer/1";request.Accept="application/vnd.github+json";request.Timeout=15000;request.ReadWriteTimeout=15000;request.AllowAutoRedirect=false;
    var response=(HttpWebResponse)request.GetResponse();
    int status=(int)response.StatusCode;
    if(status>=300 && status<400){var next=new Uri(uri,response.Headers["Location"]);response.Dispose();uri=next;continue;}
    return response;
   }
   throw new Exception("Too many download redirects.");
  }
  public static bool Apply(string game) {
   var engine=new Engine(game);engine.Check();var state=engine.ReadState();if(state==null)throw new Exception("Install the mod first.");
   Release release;
   try {using(var response=Open(Latest))using(var reader=new StreamReader(response.GetResponseStream())) {
    char[] buffer=new char[1024*1024];int length=0,n;while(length<buffer.Length && (n=reader.Read(buffer,length,buffer.Length-length))>0)length+=n;
    if(length==buffer.Length)throw new Exception("Release response too large.");release=Data.Json.Deserialize<Release>(new string(buffer,0,length));
   }}catch(WebException e){var r=e.Response as HttpWebResponse;if(r!=null&&r.StatusCode==HttpStatusCode.NotFound)return false;throw;}
   return ApplyRelease(engine,release,Download);
  }
  static void Download(ReleaseAsset asset,string temp) {
   using(var response=Open(asset.browser_download_url))using(var source=response.GetResponseStream())using(var dest=File.Create(temp)) {
    byte[] block=new byte[65536];long size=0;int n;while((n=source.Read(block,0,block.Length))>0){size+=n;if(size>asset.size)throw new Exception("Update exceeded expected size.");dest.Write(block,0,n);}
   }
  }
  public static bool ApplyRelease(Engine engine,Release release,Action<ReleaseAsset,string> download) {
   engine.Check();var state=engine.ReadState();if(state==null)throw new Exception("Install the mod first.");
   var asset=Select(release,state.Version);if(asset==null)return false;
   string temp=Path.Combine(Path.GetTempPath(),"DVR-update-"+Guid.NewGuid().ToString("N")+".zip");
   try {
    download(asset,temp);
    Verify(asset,temp);using(var package=new Package(temp)) {
     if(package.Manifest.Version!=release.tag_name.Substring(1))throw new Exception("Release/package version mismatch.");
     engine.Install(package);
    }
    return true;
   }finally{if(File.Exists(temp))File.Delete(temp);}
  }
 }
}
