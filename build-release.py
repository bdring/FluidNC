#!/usr/bin/env python

# Build FluidNC release bundles (.zip files) for each host platform

from shutil import copy
from zipfile import ZipFile, ZipInfo
import subprocess, os, sys
import urllib.request

verbose = '-v' in sys.argv

environ = dict(os.environ)

def buildEmbeddedPage():
    print('Building embedded web page')
    return subprocess.run(["python", "build.py"], cwd="embedded").returncode

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

def copyToZip(zipObj, platform, fileName, destPath, mode=0o100755):
    sourcePath = os.path.join(sharedPath, platform, fileName)
    with open(sourcePath, 'r') as f:
        bytes = f.read()
    info = ZipInfo.from_file(sourcePath, os.path.join(destPath, fileName))
    info.external_attr = mode << 16
    zipObj.writestr(info, bytes)


relPath = os.path.join('release')
if not os.path.exists(relPath):
    os.makedirs(relPath)

# We avoid doing this every time, instead checking in a new NoFile.h as necessary
# if buildEmbeddedPage() != 0:
#    sys.exit(1)

if buildFs('wifi', verbose=verbose) != 0:
    sys.exit(1)

for envName in ['wifi','bt']:
    if buildEnv(envName, verbose=verbose) != 0:
        sys.exit(1)

for platform in ['win64', 'posix']:
    print("Creating zip file for ", platform)
    terseOSName = {
        'win64': 'win',
        'posix': 'posix',
    }
    scriptExtension = {
        'win64': '.bat',
        'posix': '.sh',
    }
    exeExtension = {
        'win64': '.exe',
        'posix': '',
    }
    withEsptoolBinary = {
        'win64': True,
        'posix': False,
    }

    zipDirName = os.path.join('fluidnc-' + tag + '-' + platform)
    zipFileName = os.path.join(relPath, zipDirName + '.zip')

    with ZipFile(zipFileName, 'w') as zipObj:
        name = 'HOWTO-INSTALL.txt'
        zipObj.write(os.path.join(sharedPath, platform, name), os.path.join(zipDirName, name))

        pioPath = os.path.join('.pio', 'build')

        # Put bootloader binaries in the archive
        tools = os.path.join(os.path.expanduser('~'),'.platformio','packages','framework-arduinoespressif32','tools')
        bootloader = 'bootloader_dio_80m.bin'
        zipObj.write(os.path.join(tools, 'sdk', 'bin', bootloader), os.path.join(zipDirName, 'common', bootloader))
        bootapp = 'boot_app0.bin';
        zipObj.write(os.path.join(tools, "partitions", bootapp), os.path.join(zipDirName, 'common', bootapp))
        for secFuses in ['SecurityFusesOK.bin', 'SecurityFusesOK0.bin']:
            zipObj.write(os.path.join(sharedPath, 'common', secFuses), os.path.join(zipDirName, 'common', secFuses))

        # Put FluidNC binaries, partition maps, and installers in the archive
        for envName in ['wifi','bt']:

            # Put spiffs.bin and index.html.gz in the archive
            # bt does not need a spiffs.bin because there is no use for index.html.gz
            if envName == 'wifi':
                name = 'spiffs.bin'
                zipObj.write(os.path.join(pioPath, envName, name), os.path.join(zipDirName, envName, name))
                name = 'index.html.gz'
                zipObj.write(os.path.join('FluidNC', 'data', name), os.path.join(zipDirName, envName, name))

            objPath = os.path.join(pioPath, envName)
            for obj in ['firmware.bin','partitions.bin']:
                zipObj.write(os.path.join(objPath, obj), os.path.join(zipDirName, envName, obj))

            # E.g. posix/install-wifi.sh -> install-wifi.sh
            copyToZip(zipObj, platform, 'install-' + envName + scriptExtension[platform], zipDirName)

        for script in ['install-fs', 'fluidterm', 'checksecurity', 'erase', 'tools']:
            # E.g. posix/fluidterm.sh -> fluidterm.sh
            copyToZip(zipObj, platform, script + scriptExtension[platform], zipDirName)

        # Put the fluidterm code in the archive
        for obj in ['fluidterm.py', 'README-FluidTerm.md']:
            fn = os.path.join('fluidterm', obj)
            zipObj.write(fn, os.path.join(zipDirName, os.path.join('common', obj)))

        if platform == 'win64':
            obj = 'fluidterm' + exeExtension[platform]
            zipObj.write(os.path.join('fluidterm', obj), os.path.join(zipDirName, platform, obj))

        EsptoolVersion = 'v3.1'

        # Put esptool and related tools in the archive
        if withEsptoolBinary[platform]:
            name = 'README-ESPTOOL.txt'
            EspRepo = 'https://github.com/espressif/esptool/releases/download/' + EsptoolVersion + '/'
            EspDir = 'esptool-' + EsptoolVersion + '-' + platform
            zipObj.write(os.path.join(sharedPath, platform, name), os.path.join(zipDirName, platform,
                         name.replace('.txt', '-' + EsptoolVersion + '.txt')))
        else:
            name = 'README-ESPTOOL-SOURCE.txt'
            EspRepo = 'https://github.com/espressif/esptool/archive/refs/tags/'
            EspDir = EsptoolVersion
            zipObj.write(os.path.join(sharedPath, 'common', name), os.path.join(zipDirName, 'common',
                         name.replace('.txt', '-' + EsptoolVersion + '.txt')))


        # Download and unzip from ESP repo
        ZipFileName = EspDir + '.zip'
        if not os.path.isfile(ZipFileName):
            with urllib.request.urlopen(EspRepo + ZipFileName) as u:
                open(ZipFileName, 'wb').write(u.read())

        if withEsptoolBinary[platform]:
            for Binary in ['esptool']:
                Binary += exeExtension[platform]
                sourceFileName = EspDir + '/' + Binary
                with ZipFile(ZipFileName, 'r') as zipReader:
                    destFileName = os.path.join(zipDirName, platform, Binary)
                    info = ZipInfo(destFileName)
                    info.external_attr = 0o100755 << 16
                    zipObj.writestr(info, zipReader.read(sourceFileName))
        else:
            zipObj.write(os.path.join(ZipFileName), os.path.join(zipDirName, 'common', 'esptool-source.zip'))

sys.exit(0)
