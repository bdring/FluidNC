#!/usr/bin/env python

# Compile FluidNC for each of the radio modes
# Add-on files are built on top of a single base.
# This is useful for automated testing, to make sure you haven't broken something

# The output is filtered so that the only lines you see are a single
# success or failure line for each build, plus any preceding lines that
# contain the word "error".  If you need to see everything, for example to
# see the details of an errored build, include -v on the command line.

from shutil import copy
from zipfile import ZipFile, ZipInfo
import subprocess, os, sys
import urllib.request

verbose = '-v' in sys.argv

environ = dict(os.environ)

def buildEnv(pioEnv, verbose=True, extraArgs=None):
    cmd = ['platformio','run', "-e", pioEnv]
    if extraArgs:
        cmd.append(extraArgs)
    displayName = pioEnv
    print('Building firmware for ' + displayName)
    if verbose:
        app = subprocess.Popen(cmd, env=environ)
    else:
        app = subprocess.Popen(cmd, env=environ, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=1)
        for line in app.stdout:
            line = line.decode('utf8')
            if "Took" in line or 'Uploading' in line or ("error" in line.lower() and "Compiling" not in line):
                print(line, end='')
    app.wait()
    print()
    return app.returncode

def buildFs(pioEnv, verbose=True, extraArgs=None):
    cmd = ['platformio','run', '-e', pioEnv, '-t', 'buildfs']
    if extraArgs:
        cmd.append(extraArgs)
    print('Building file system for ' + pioEnv)
    if verbose:
        app = subprocess.Popen(cmd, env=environ)
    else:
        app = subprocess.Popen(cmd, env=environ, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=1)
        for line in app.stdout:
            line = line.decode('utf8')
            if "Took" in line or 'Uploading' in line or ("error" in line.lower() and "Compiling" not in line):
                print(line, end='')
    app.wait()
    print()
    return app.returncode

tag = (
    subprocess.check_output(["git", "describe", "--tags", "--abbrev=0"])
    .strip()
    .decode("utf-8")
)

sharedPath = 'install_scripts'
relPath = os.path.join('release')
if not os.path.exists(relPath):
    os.makedirs(relPath)

zipFileName = os.path.join(relPath, 'fluidnc-' + tag + '.zip')

with ZipFile(zipFileName, 'w') as zipObj:
    name = 'HOWTO-INSTALL.txt'
    zipObj.write(os.path.join(sharedPath, name), name)
    name = 'README-ESPTOOL.txt'
    zipObj.write(os.path.join(sharedPath, name), name)

    numErrors = 0

    pioPath = os.path.join('.pio', 'build')

    exitCode = buildFs('wifi', verbose=verbose)
    if exitCode != 0:
        numErrors += 1
    else:
        # Put common/spiffs.bin in the archive
        obj = 'spiffs.bin'
        zipObj.write(os.path.join(pioPath, 'wifi', obj), os.path.join('common', obj))
        tools = os.path.join(os.path.expanduser('~'),'.platformio','packages','framework-arduinoespressif32','tools')
        bootloader = 'bootloader_dio_80m.bin'
        zipObj.write(os.path.join(tools, 'sdk', 'bin', bootloader), os.path.join('common', bootloader))
        bootapp = 'boot_app0.bin';
        zipObj.write(os.path.join(tools, "partitions", bootapp), os.path.join('common', bootapp))

        for envName in ['wifi','bt']:
            exitCode = buildEnv(envName, verbose=verbose)
            if exitCode != 0:
                numErrors += 1
            else:
                objPath = os.path.join(pioPath, envName)
                for obj in ['firmware.bin','partitions.bin']:
                    zipObj.write(os.path.join(objPath, obj), os.path.join(envName, obj))
                for obj in ['install-win.bat','install-linux.sh','install-macos.sh']:
                    sourceFileName = os.path.join(sharedPath, obj)
                    destFileName = os.path.join(envName, obj)
                    with open(sourceFileName, 'r') as f:
                        bytes = f.read()
                    info = ZipInfo.from_file(sourceFileName, destFileName)
                    info.external_attr = 0o100755 << 16
                    zipObj.writestr(info, bytes)
        EsptoolVersion = 'v3.1'
        EspRepo = 'https://github.com/espressif/esptool/releases/download/' + EsptoolVersion + '/'

        for platform in ['win64', 'macos', 'linux-amd64']:
            EspDir = 'esptool-' + EsptoolVersion + '-' + platform
            # Download and unzip from es
            ZipFileName = EspDir + '.zip'

            if not os.path.isfile(ZipFileName):
                print('Downloading ' + EspRepo + ZipFileName)
                with urllib.request.urlopen(EspRepo + ZipFileName) as u:
                    open(ZipFileName, 'wb').write(u.read())
            Binary = 'esptool'
            if platform == 'win64':
                Binary += '.exe'
            sourceFileName = EspDir + '/' + Binary
            with ZipFile(ZipFileName, 'r') as zipReader:
                destFileName = os.path.join(platform, Binary)
                info = ZipInfo(destFileName)
                info.external_attr = 0o100755 << 16
                zipObj.writestr(info, zipReader.read(sourceFileName))

sys.exit(numErrors)
