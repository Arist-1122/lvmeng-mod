using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Web.Script.Serialization;

class LaunchPolicyProbe {
    static readonly List<object> results=new List<object>();
    static void Check(string name,bool value){results.Add(new{name=name,passed=value});if(!value)throw new Exception(name);}
    static int Main(string[] args){
        if(args.Length==1&&args[0]=="--owner"){Thread.Sleep(1200);return 0;}
        try {
            int combinations=0;
            for(int bits=0;bits<32;bits++){
                bool admin=(bits&1)!=0,shell=(bits&2)!=0,sameSession=(bits&4)!=0,sameUser=(bits&8)!=0,shellAdmin=(bits&16)!=0;
                bool rejects=admin&&shell&&(!sameSession||!sameUser);string expected=!admin?"current-user":!shell||shellAdmin?"existing-environment":"desktop-user";
                string actual=null;bool rejected=false;
                try {actual=LaunchPrivileges.ChooseRoute(admin,shell,sameSession,sameUser,shellAdmin);}
                catch(Exception){rejected=true;}
                Check("policy_"+bits,rejects?rejected:!rejected&&actual==expected);
                combinations++;
            }
            var probe=LaunchPrivileges.Inspect();Check("ordinary_inspection_needs_no_shell_token",probe.currentAdministrator||(probe.route=="current-user"&&!probe.shellInspected));
            string self;using(var current=Process.GetCurrentProcess()){
                self=current.MainModule.FileName;long ticks=current.StartTime.ToUniversalTime().Ticks;
                Check("live_owner",LaunchPrivileges.MatchesProcessIdentity(current.Id,ticks,self));
                Check("wrong_owner_time",!LaunchPrivileges.MatchesProcessIdentity(current.Id,ticks+1,self));
                Check("wrong_owner_path",!LaunchPrivileges.MatchesProcessIdentity(current.Id,ticks,self+".other"));
            }
            Check("invalid_owner",!LaunchPrivileges.MatchesProcessIdentity(0,1,self));
            using(var owner=Process.Start(new ProcessStartInfo(self,"--owner"){UseShellExecute=false,CreateNoWindow=true,WindowStyle=ProcessWindowStyle.Hidden})){
                // Keep an actual process handle open after exit, reproducing
                // the stale-owner case without executing a game or loader.
                IntPtr retained=owner.Handle;long ticks=owner.StartTime.ToUniversalTime().Ticks;
                Check("child_live_owner",LaunchPrivileges.MatchesProcessIdentity(owner.Id,ticks,self));
                Check("child_finished",owner.WaitForExit(5000));
                Check("exited_owner_with_retained_handle",!LaunchPrivileges.MatchesProcessIdentity(owner.Id,ticks,self));
            }
            if(!probe.currentAdministrator){
                bool rejected=false;try{LaunchPrivileges.RunDesktopBridge(self,"--owner",Path.GetDirectoryName(self));}catch{rejected=true;}
                Check("bridge_not_spawned_without_desktop_route",rejected);
            }
            var report=new{passed=true,policyCombinations=combinations,checks=results,privileges=probe,gameLaunched=false,loaderInvoked=false,adminRequested=false,actualElevatedBridgeTested=false};
            File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"launch_policy_probe.json"),new JavaScriptSerializer().Serialize(report));return 0;
        }catch(Exception error){File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"launch_policy_probe.json"),new JavaScriptSerializer().Serialize(new{passed=false,error=error.ToString(),checks=results,gameLaunched=false,loaderInvoked=false,adminRequested=false}));return 2;}
    }
}
