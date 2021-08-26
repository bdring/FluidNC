# This script is imported by build-release.py
# It performs a platformio build with a given environment
# The verbose argument controls whether the full output is
# displayed, or filtered to show only summary information.
# extraArgs can be used to perform uploading after compilation.

from __future__ import print_function
import subprocess, os

env = dict(os.environ)

def buildEnv(envName, verbose=True, extraArgs=None):
    cmd = ['platformio','run', "-e", envName]
    if extraArgs:
        cmd.append(extraArgs)
    displayName = envName
    print('Building firmware for ' + displayName)
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

def buildFs(envName, verbose=True, extraArgs=None):
    cmd = ['platformio','run', '-e', envName, '-t', 'buildfs']
    if extraArgs:
        cmd.append(extraArgs)
    print('Building file system for ' + envName)
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
