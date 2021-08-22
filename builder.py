# This script is imported by build-machine.py and build-all.py
# It performs a platformio build with a given machine file.
# The verbose argument controls whether the full output is
# displayed, or filtered to show only summary information.
# extraArgs can be used to perform uploading after compilation.

from __future__ import print_function
import subprocess, os

env = dict(os.environ)

def buildMachine(baseName, verbose=True, extraArgs=None):
    cmd = ['platformio','run', "-e", baseName]
    if extraArgs:
        cmd.append(extraArgs)
    displayName = baseName
    print('Building machine ' + displayName)
    if verbose:
        app = subprocess.Popen(cmd, env=env)
    else:
        app = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=1)
        for line in app.stdout:
            line = line.decode('utf8')
            if "Took" in line or 'Uploading' in line or ("error" in line.lower() and "Compiling" not in line):
                print(line, end='')
    app.wait()
    print()
    return app.returncode
