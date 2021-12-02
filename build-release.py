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
    cmd = ['platformio','run', '-e', pioEnv, '-t', 'release', '-t', 'buildfs']
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
    # The following ought to work but it does not because of
    # https://github.com/platformio/platformio-core/issues/4125
    # cmdEnv = dict(os.environ, PLATFORMIO_DATA_DIR="data-"+pioEnv)
    cmdEnv = environ
    if True:
        app = subprocess.Popen(cmd, env=cmdEnv)
    else:
        app = subprocess.Popen(cmd, env=cmdEnv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=1)
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

# numErrors = 0
# if buildFs('wifi', verbose=verbose) != 0:
#     numErrors += 1

# if numErrors:
#     sys.exit(numErrors)

for envName in ['wifi','bt']:
    if buildEnv(envName, verbose=True) != 0:
        sys.exit(1)
    print(os.listdir('.pio/build'))
    print(os.listdir('.pio/build/' + envName))

print(os.listdir('.pio/build'))

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

        # Put FluidNC binaries, partition maps, and installers in the archive
        for envName in ['wifi','bt']:

            # Put spiffs.bin and index.html.gz in the archive
            # bt does not need a spiffs.bin because there is no use for index.html.gz
            if envName == 'wifi':
                name = 'spiffs.bin'
                print("From ", os.path.join(pioPath, envName, name), " to ", os.path.join(envName, name))
                zipObj.write(os.path.join(pioPath, envName, name), os.path.join(envName, name))
                name = 'index.html.gz'
                zipObj.write(os.path.join('FluidNC', 'data', name), os.path.join(envName, name))

            objPath = os.path.join(pioPath, envName)
            for obj in ['firmware.bin','partitions.bin']:
                zipObj.write(os.path.join(objPath, obj), os.path.join(envName, obj))
            
            scriptName = 'install-' + envName + scriptExtension[platform]

            sourceFileName = os.path.join(sharedPath, platform, scriptName)
            with open(sourceFileName, 'r') as f:
                bytes = f.read()
            info = ZipInfo.from_file(sourceFileName, scriptName)
            info.external_attr = 0o100755 << 16
            zipObj.writestr(info, bytes)

        scriptName = 'install-fs' + scriptExtension[platform]

        sourceFileName = os.path.join(sharedPath, platform, scriptName)
        with open(sourceFileName, 'r') as f:
            bytes = f.read()
        info = ZipInfo.from_file(sourceFileName, scriptName)
        info.external_attr = 0o100755 << 16
        zipObj.writestr(info, bytes)

        # Put esptool and related tools in the archive
        EsptoolVersion = 'v3.1'
        EspRepo = 'https://github.com/espressif/esptool/releases/download/' + EsptoolVersion + '/'

        EspDir = 'esptool-' + EsptoolVersion + '-' + platform
        # Download and unzip from ESP repo
        ZipFileName = EspDir + '.zip'
        if not os.path.isfile(ZipFileName):
            with urllib.request.urlopen(EspRepo + ZipFileName) as u:
                open(ZipFileName, 'wb').write(u.read())
        for Binary in ['esptool', 'espefuse']:
            Binary += exeExtension[platform]
            sourceFileName = EspDir + '/' + Binary
            with ZipFile(ZipFileName, 'r') as zipReader:
                destFileName = os.path.join(platform, Binary)
                info = ZipInfo(destFileName)
                info.external_attr = 0o100755 << 16
                zipObj.writestr(info, zipReader.read(sourceFileName))

sys.exit(numErrors)
