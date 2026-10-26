using System;
using System.IO;
using System.Linq;
using System.Diagnostics;
using System.Drawing;
using System.Windows.Forms;
using System.Threading.Tasks;

namespace DvrInstaller {
 public class Preferences {public string Game; public string Headset="auto";public bool AutoUpdate=true;}
 static class Program {
  public static readonly string Folder=AppDomain.CurrentDomain.BaseDirectory;
  public static string PrefPath {get{return Path.Combine(Folder,"installation.json");}}
  public static Preferences Pref {get{return File.Exists(PrefPath)?Data.Load<Preferences>(PrefPath):new Preferences();}}
  public static void Install(string game,string headset,bool auto) {
   var old=Pref;
   if(!String.IsNullOrEmpty(old.Game)&&!Path.GetFullPath(old.Game).Equals(Path.GetFullPath(game),StringComparison.OrdinalIgnoreCase)&&new Engine(old.Game).ReadState()!=null)
    throw new Exception("Remove the managed installation from "+old.Game+" before selecting a different game folder.");
   using(var p=new Package(Path.Combine(Folder,"payload.zip")))new Engine(game).Install(p,headset);
   // Existing settings are never rewritten, including the user's trigger preference.
   Data.Save(PrefPath,new Preferences{Game=Path.GetFullPath(game),Headset=headset,AutoUpdate=auto});
  }
  [STAThread] public static int Main(string[] args) {
   try {
    if(args.Length>0 && args[0]=="--discover") {File.WriteAllLines(args[1],Discovery.Find());return 0;}
    if(args.Length>0 && args[0]=="--toolkit") {File.WriteAllText(args[1],Discovery.ToolkitFound()?"PSVR2 Toolkit component detected. Live trigger operation still requires SteamVR and connected controllers.":"PSVR2 Toolkit was not detected. The mod will work without it, but adaptive triggers require its separate official installation.");return 0;}
    if(args.Length>0 && args[0]=="--check") {new Engine(args[1]).Check();return 0;}
    if(args.Length>0 && args[0]=="--install") {Install(args[1],args[2],args[3]=="1");return 0;}
    if(args.Length>0 && args[0]=="--uninstall") {var pref=Pref;if(!String.IsNullOrEmpty(pref.Game))new Engine(pref.Game).Uninstall();return 0;}
    Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
    Application.Run(new ManagerForm(args.Contains("--play")));return 0;
   }catch(Exception e){MessageBox.Show(e.Message,"Dishonored 2 VR",MessageBoxButtons.OK,MessageBoxIcon.Error);return 1;}
  }
 }
 public class ManagerForm : Form {
  Label info=new Label(),status=new Label();CheckBox automatic=new CheckBox();Button update=new Button(),play=new Button(),rollback=new Button();bool launch;
  public ManagerForm(bool autoPlay) {
   launch=autoPlay;Text="Dishonored 2 VR";ClientSize=new Size(640,345);Font=new Font("Segoe UI",10);StartPosition=FormStartPosition.CenterScreen;FormBorderStyle=FormBorderStyle.FixedDialog;MaximizeBox=false;
   var title=new Label{Text="Dishonored 2 VR",Font=new Font("Segoe UI",20,FontStyle.Bold),Location=new Point(24,18),Size=new Size(580,40)};Controls.Add(title);
   info.SetBounds(24,72,590,65);Controls.Add(info);
   automatic.Text="Automatically install stable updates before launching through this app";automatic.SetBounds(24,142,590,30);automatic.Checked=Program.Pref.AutoUpdate;
   automatic.CheckedChanged+=(s,e)=>{var p=Program.Pref;p.AutoUpdate=automatic.Checked;Data.Save(Program.PrefPath,p);};Controls.Add(automatic);
   play.Text="Play";play.SetBounds(24,193,175,38);play.Click+=async(s,e)=>await Run(true);Controls.Add(play);
   update.Text="Check for updates";update.SetBounds(214,193,185,38);update.Click+=async(s,e)=>await Run(false);Controls.Add(update);
   rollback.Text="Roll back";rollback.SetBounds(414,193,175,38);rollback.Click+=(s,e)=>{try{new Engine(Program.Pref.Game).Rollback();automatic.Checked=false;var pref=Program.Pref;pref.AutoUpdate=false;Data.Save(Program.PrefPath,pref);RefreshInfo();status.Text="Previous installation restored. Automatic updates paused so the rolled-back version stays installed. Your settings were kept.";}catch(Exception x){status.Text=x.Message;}};Controls.Add(rollback);
   status.SetBounds(24,247,590,75);status.Text="Updates come only from Indurhra/dishonored-2-vr. Saves and settings are preserved.";Controls.Add(status);
   Shown+=async(s,e)=>{RefreshInfo();if(launch)await Run(true);};
  }
  void RefreshInfo() {
   var p=Program.Pref;var state=String.IsNullOrEmpty(p.Game)?null:new Engine(p.Game).ReadState();
   info.Text=(state==null?"Mod is not installed.":"Installed version: "+state.Version)+"\n"+(p.Game??"");play.Enabled=update.Enabled=rollback.Enabled=state!=null;
  }
  async Task Run(bool startGame) {
   play.Enabled=update.Enabled=rollback.Enabled=automatic.Enabled=false;bool launchAllowed=true;
   try {
    if(!startGame||automatic.Checked){status.Text="Checking for a verified stable update…";bool changed=await Task.Run(()=>Updates.Apply(Program.Pref.Game));status.Text=changed?"Update installed successfully.":"No newer stable update is available.";}
   }catch(Exception e){status.Text="Update was not applied: "+e.Message;launchAllowed=MessageBox.Show(this,status.Text+(startGame?"\nLaunch the existing installation?":""),"Update unavailable",startGame?MessageBoxButtons.YesNo:MessageBoxButtons.OK,MessageBoxIcon.Information)==DialogResult.Yes;}
   finally{automatic.Enabled=true;RefreshInfo();}
   if(startGame&&launchAllowed){Process.Start(new ProcessStartInfo("steam://rungameid/403640"){UseShellExecute=true});Close();}
  }
 }
}
