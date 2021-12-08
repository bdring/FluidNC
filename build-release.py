#!/usr/bin/env python

# Build FluidNC release bundles (.zip files) for each host platform

from shutil import copy
from zipfile import ZipFile, ZipInfo
import subprocess, os, sys
import urllib.request

verbose = '-v' in sys.argv

environ = dict(os.environ)

def buildEnv(pioEnv, verbose=True, extraArgs=None):
    cmd = ['platformio','run', '--disable-auto-clean', '-e', pioEnv]
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

def buildFs(pioEnv, verbose=verbose, extraArgs=None):
    cmd = ['platformio','run', '--disable-auto-clean', '-e', pioEnv, '-t', 'buildfs']
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

def copyToZip(zipObj, platform, destPath, mode=0o100755):
    sourcePath = os.path.join(sharedPath, platform, destPath)
    with open(sourcePath, 'r') as f:
        bytes = f.read()
    info = ZipInfo.from_file(sourcePath, destPath)
    info.external_attr = mode << 16
    zipObj.writestr(info, bytes)


relPath = os.path.join('release')
if not os.path.exists(relPath):
    os.makedirs(relPath)

if buildFs('wifi', verbose=verbose) != 0:
    sys.exit(1)

for envName in ['wifi','bt']:
    if buildEnv(envName, verbose=verbose) != 0:
        sys.exit(1)

for platform in ['win64', 'macos', 'linux-amd64']:
    print("Creating zip file for ", platform)
    terseOSName = {
        'win64': 'win',
        'linux-amd64': 'linux',
        'macos': 'macos'
    }
    scriptExtension = {
        'win64': '.bat',
        'linux-amd64': '.sh',
        'macos': '.sh'
    }
    exeExtension = {
        'win64': '.exe',
        'linux-amd64': '',
        'macos': ''
    }

    zipFileName = os.path.join(relPath, 'fluidnc-' + tag + '-' + platform + '.zip')
    
    with ZipFile(zipFileName, 'w') as zipObj:
        name = 'HOWTO-INSTALL.txt'
        zipObj.write(os.path.join(sharedPath, platform, name), name)
        name = 'README-ESPTOOL.txt'
        zipObj.write(os.path.join(sharedPath, name), os.path.join(platform, name))
    
        pioPath = os.path.join('.pio', 'build')
    
        # Put bootloader binaries in the archive
        tools = os.path.join(os.path.expanduser('~'),'.platformio','packages','framework-arduinoespressif32','tools')
        bootloader = 'bootloader_dio_80m.bin'
        zipObj.write(os.path.join(tools, 'sdk', 'bin', bootloader), os.path.join('common', bootloader))
        bootapp = 'boot_app0.bin';
        zipObj.write(os.path.join(tools, "partitions", bootapp), os.path.join('common', bootapp))
        secFuses = 'SecurityFusesOK.bin';
        zipObj.write(os.path.join(sharedPath, secFuses), os.path.join('common', secFuses))

        # Put FluidNC binaries, partition maps, and installers in the archive
        for envName in ['wifi','bt']:

            # Put spiffs.bin and index.html.gz in the archive
            # bt does not need a spiffs.bin because there is no use for index.html.gz
            if envName == 'wifi':
                name = 'spiffs.bin'
                zipObj.write(os.path.join(pioPath, envName, name), os.path.join(envName, name))
                name = 'index.html.gz'
                zipObj.write(os.path.join('FluidNC', 'data', name), os.path.join(envName, name))

            objPath = os.path.join(pioPath, envName)
            for obj in ['firmware.bin','partitions.bin']:
                zipObj.write(os.path.join(objPath, obj), os.path.join(envName, obj))
            
            # E.g. macos/install-wifi.sh -> install-wifi.sh
            copyToZip(zipObj, platform, 'install-' + envName + scriptExtension[platform])

        for script in ['install-fs', 'fluidterm', 'checksecurity', 'tools', ]:
            # E.g. macos/fluidterm.sh -> fluidterm.sh
            copyToZip(zipObj, platform, script + scriptExtension[platform])

        # Put the fluidterm code in the archive
        for obj in ['fluidterm.py', 'README.md']:
            fn = os.path.join('fluidterm', obj)
            zipObj.write(fn, fn)
            
        if platform == 'win64':
            obj = 'fluidterm' + exeExtension[platform]
            zipObj.write(os.path.join('fluidterm', obj), os.path.join(platform, obj))

        # Put esptool and related tools in the archive
        EsptoolVersion = 'v3.1'
        EspRepo = 'https://github.com/espressif/esptool/releases/download/' + EsptoolVersion + '/'

        EspDir = 'esptool-' + EsptoolVersion + '-' + platform
        # Download and unzip from ESP repo
        ZipFileName = EspDir + '.zip'
        if not os.path.isfile(ZipFileName):
            with urllib.request.urlopen(EspRepo + ZipFileName) as u:
                open(ZipFileName, 'wb').write(u.read())
        for Binary in ['esptool']:
            Binary += exeExtension[platform]
            sourceFileName = EspDir + '/' + Binary
            with ZipFile(ZipFileName, 'r') as zipReader:
                destFileName = os.path.join(platform, Binary)
                info = ZipInfo(destFileName)
                info.external_attr = 0o100755 << 16
                zipObj.writestr(info, zipReader.read(sourceFileName))

sys.exit(0)
