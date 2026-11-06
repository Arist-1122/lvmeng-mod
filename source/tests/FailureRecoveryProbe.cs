using System;
using System.IO;
using System.Reflection;
using System.Web.Script.Serialization;
using System.Windows.Forms;
class FailureRecoveryProbe {
    static readonly BindingFlags Private=BindingFlags.Instance|BindingFlags.NonPublic;
    [STAThread]static int Main(){
        ModelTool form=null;
        try {
            form=(ModelTool)Activator.CreateInstance(typeof(ModelTool),true);
            string session=Guid.NewGuid().ToString("N");
            string malformed=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"malformed_helper_state.json");File.WriteAllText(malformed,"not valid JSON");
            typeof(ModelTool).GetField("package",Private).SetValue(form,new ModelTool.Package{runtimeReady=true});
            typeof(ModelTool).GetField("sessionId",Private).SetValue(form,session);
            typeof(ModelTool).GetField("helperStatePath",Private).SetValue(form,malformed);
            typeof(ModelTool).GetField("helperPending",Private).SetValue(form,true);
            typeof(ModelTool).GetField("wantReplacement",Private).SetValue(form,true);
            var start=(Button)typeof(ModelTool).GetField("start",Private).GetValue(form);start.Enabled=false;
            typeof(ModelTool).GetMethod("Tick",Private).Invoke(form,null);
            bool enabled=start.Enabled,sessionCleared=typeof(ModelTool).GetField("sessionId",Private).GetValue(form)==null;
            bool pendingCleared=!(bool)typeof(ModelTool).GetField("helperPending",Private).GetValue(form);
            bool replacementStopped=!(bool)typeof(ModelTool).GetField("wantReplacement",Private).GetValue(form);
            bool cancelled=File.Exists(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"control","cancel_"+session+".json"));
            bool passed=enabled&&sessionCleared&&pendingCleared&&replacementStopped&&cancelled;
            File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"failure_recovery_probe.json"),new JavaScriptSerializer().Serialize(new{passed=passed,startEnabled=enabled,sessionCleared=sessionCleared,helperPendingCleared=pendingCleared,replacementStopped=replacementStopped,cancellationRecorded=cancelled,windowShown=false,gameLaunched=false,loaderInvoked=false,adminRequested=false}));
            return passed?0:2;
        }catch(Exception error){File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"failure_recovery_probe.json"),new JavaScriptSerializer().Serialize(new{passed=false,error=error.ToString(),gameLaunched=false,loaderInvoked=false,adminRequested=false}));return 2;}
        finally{if(form!=null){((System.Windows.Forms.Timer)typeof(ModelTool).GetField("timer",Private).GetValue(form)).Dispose();form.Dispose();}}
    }
}
