"""Reproduce launch policy, process-owner and failure-recovery checks.

No game or loader is started and no elevation is requested. Only the test
executable's bounded child process and an unshown WinForms instance are used.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

SOURCE = Path(__file__).resolve().parent
CSC = Path(os.environ.get('WINDIR', r'C:\Windows')) / 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'


def check(output):
    output.mkdir(parents=True, exist_ok=True)
    refs = ['System.Web.Extensions.dll', 'System.Windows.Forms.dll', 'System.Drawing.dll']
    production = ['common/LaunchPrivileges.cs', 'common/GameCompatibility.cs',
                  'managed/GameLocation.cs', 'managed/ModelTool.cs']
    flags = subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS
    results = {}
    for name, files, receipt in [
        ('LaunchPolicyProbe', ['tests/LaunchPolicyProbe.cs', 'common/LaunchPrivileges.cs'], 'launch_policy_probe.json'),
        ('FailureRecoveryProbe', ['tests/FailureRecoveryProbe.cs'] + production, 'failure_recovery_probe.json'),
    ]:
        exe = output / (name + '.exe')
        command = [str(CSC), '/nologo', '/utf8output', '/platform:x64', '/optimize+', '/target:exe',
                   '/main:' + name, '/out:' + str(exe), *('/r:' + r for r in refs),
                   *(str(SOURCE / p) for p in files)]
        compiled = subprocess.run(command, capture_output=True, timeout=60, creationflags=flags)
        (output / (name + '.compile.log')).write_bytes(compiled.stdout + compiled.stderr)
        if compiled.returncode:
            raise RuntimeError((compiled.stdout + compiled.stderr).decode('utf-8', errors='replace'))
        ran = subprocess.run([str(exe)], capture_output=True, timeout=15, creationflags=flags)
        data = json.loads((output / receipt).read_text(encoding='utf-8-sig'))
        if ran.returncode or not data['passed']:
            raise RuntimeError(name + ' failed: ' + json.dumps(data, ensure_ascii=False))
        results[name] = data
    result = {'passed': True, 'tests': results,
              'sourceSha256': {p: hashlib.sha256((SOURCE / p).read_bytes()).hexdigest() for p in production},
              'gameLaunched': False, 'loaderInvoked': False, 'adminRequested': False,
              'actualElevatedBridgeTested': False}
    (output / 'launch_tests.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    options = parser.parse_args()
    result = check(options.output.resolve())
    print(json.dumps({'passed': result['passed'],
                      'policyAndOwnerChecks': len(result['tests']['LaunchPolicyProbe']['checks']),
                      'failureRecoveryPassed': result['tests']['FailureRecoveryProbe']['passed'],
                      'gameLaunched': False, 'adminRequested': False}))
