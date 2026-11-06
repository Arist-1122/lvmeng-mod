using System;
using System.Web.Script.Serialization;
class CompatibilityCheck {
    static int Main(string[] args) {
        if(args.Length!=3||args[0]!="--check-compatibility"){Console.Error.WriteLine("Usage: CompatibilityCheck.exe --check-compatibility <runtime-root> <game-Lumin.exe>");return 2;}
        return GameCompatibility.RunReadOnlyCli(args[1],args[2]);
    }
}
