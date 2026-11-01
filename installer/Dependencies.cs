using System;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Linq;
using System.Collections.Generic;

namespace DvrInstaller {
 public static class Dependencies {
  public const string AfwHash="48d5849f64ab1b9ab2717247562afc203cd92900c901510983c745618cc9e5d5";
  public const string DlssHash="be6e434a94ca32499515eb62ca0e6c274526055d568d0426e4c652dcdfb6ee6e";
  public static readonly string[] Names={"PDAFWPlugin.dll","nvngx_dlss.dll"};
  public static List<string> Problems(string root,State state=null) {
   var errors=new List<string>();foreach(string name in Names){string p=Path.Combine(root,name);if(!File.Exists(p)){errors.Add("Missing "+name);continue;}string hash=Data.HashFile(p);var owned=state==null?null:state.Files.Find(x=>x.Path.Equals(name,StringComparison.OrdinalIgnoreCase));string expected=owned==null?(name==Names[0]?AfwHash:DlssHash):owned.InstalledHash;if(hash!=expected)errors.Add("Untested version of "+name);}
   return errors;
  }
  public static int Renderer(string root) {
   string section="";int renderer=0;foreach(string raw in File.ReadAllLines(Path.Combine(root,"DishonoredVR.ini"))){string line=raw.Trim();if(line.StartsWith("[")&&line.EndsWith("]")){section=line;continue;}if(!section.Equals("[Display]",StringComparison.OrdinalIgnoreCase))continue;var pair=line.Split(new[]{'='},2);if(pair.Length==2&&pair[0].Trim().Equals("Renderer",StringComparison.OrdinalIgnoreCase)){if(!Int32.TryParse(pair[1].Split(';')[0].Trim(),out renderer)||renderer<0||renderer>7)throw new Exception("Invalid Display.Renderer setting.");}}
   return renderer;
  }
 }
}
