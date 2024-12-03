#!/usr/bin/env python

# Build FluidNC release bundles (.zip files) for each host platform

from shutil import copy
from zipfile import ZipFile, ZipInfo
import subprocess, os, sys, shutil
import urllib.request
import io, hashlib

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
        app = subprocess.Popen(cmd, env=environ, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
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
        app = subprocess.Popen(cmd, env=environ, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
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

manifestRelPath = os.path.join(relPath, 'current')
if os.path.exists(manifestRelPath):
    shutil.rmtree(manifestRelPath)

os.makedirs(manifestRelPath)

# Copy the web application to the release directory
dataRelPath = os.path.join(manifestRelPath, 'data')
os.makedirs(dataRelPath)
shutil.copy(os.path.join("FluidNC", "data", "index.html.gz"), os.path.join(dataRelPath, "index-webui-2.html.gz"))
urllib.request.urlretrieve("https://github.com/michmela44/ESP3D-WEBUI/releases/download/3.0.0-a81-FluidNC/index.html.gz", os.path.join("release", "current", "data", "index-webui-3.html.gz"))

manifest = {
        "name": "FluidNC",
        "version": tag,
        "source_url": "https://github.com/bdring/FluidNC/tree/" + tag,
        "release_url": "https://github.com/bdring/FluidNC/releases/tag/" + tag,
        "funding_url": "https://www.paypal.com/donate/?hosted_button_id=8DYLB6ZYYDG7Y",
        "images": {},
        "files": {},
        "upload": {
            "name": "upload",
            "description": "Things you can upload to the file system",
            "choice-name": "Upload group",
            "choices": []
        },
        "installable": {
            "name": "installable",
            "description": "Things you can install",
            "choice-name": "Processor type",
            "choices": []
        },
}

# We avoid doing this every time, instead checking in a new NoFile.h as necessary
# if buildEmbeddedPage() != 0:
#    sys.exit(1)

# if buildFs('wifi', verbose=verbose) != 0:
#     sys.exit(1)

def addImage(name, offset, filename, srcpath, dstpath):
    fulldstpath = os.path.join(manifestRelPath,os.path.normpath(dstpath))

    os.makedirs(fulldstpath, exist_ok=True)

    fulldstfile = os.path.join(fulldstpath, filename)

    shutil.copy(os.path.join(srcpath, filename), fulldstfile)

    print("image ", name)

    with open(fulldstfile, "rb") as f:
        data = f.read()
    image = {
        # "name": name,
        "size": os.path.getsize(fulldstfile),
        "offset": offset,
        "path": dstpath + '/' + filename,
        "signature": {
            "algorithm": "SHA2-256",
            "value": hashlib.sha256(data).hexdigest()
        }
    }
    if manifest['images'].get(name) != None:
        print("Duplicate image name", name)
        sys.exit(1)
    manifest['images'][name] = image
    # manifest['images'].append(image)

def addFile(name, controllerpath, filename, srcpath, dstpath):
    fulldstpath = os.path.join(manifestRelPath,os.path.normpath(dstpath))

    os.makedirs(fulldstpath, exist_ok=True)

    fulldstfile = os.path.join(fulldstpath, filename)

    # Only copy files that are not already in the directory
    if os.path.join(srcpath, filename) != fulldstfile:
        shutil.copy(os.path.join(srcpath, filename), fulldstfile)

    print("file ", name)

    with open(fulldstfile, "rb") as f:
        data = f.read()
    file = {
        "size": os.path.getsize(fulldstfile),
        "controller-path": controllerpath,
        "path": dstpath + '/' + filename,
        "signature": {
            "algorithm": "SHA2-256",
            "value": hashlib.sha256(data).hexdigest()
        }
    }
    if manifest['files'].get(name) != None:
        print("Duplicate file name", name)
        sys.exit(1)
    manifest['files'][name] = file
    # manifest['images'].append(image)

flashsize = "4m"

mcu = "esp32"
for mcu in ['esp32']:
    for envName in ['wifi','bt', 'noradio']:
        if buildEnv(envName, verbose=verbose) != 0:
            sys.exit(1)
        buildDir = os.path.join('.pio', 'build', envName)
        shutil.copy(os.path.join(buildDir, 'firmware.elf'), os.path.join(relPath, envName + '-' + 'firmware.elf'))

        addImage(mcu + '-' + envName + '-firmware', '0x10000', 'firmware.bin', buildDir, mcu + '/' + envName)

        if envName == 'wifi':
            if buildFs('wifi', verbose=verbose) != 0:
                sys.exit(1)

            # bootapp is a data partition that the bootloader and OTA use to determine which
            # image to run.  Its initial value is in a file "boot_app0.bin" in the platformio
            # framework package.  We copy it to the build directory so addImage can find it
            bootappsrc = os.path.join(os.path.expanduser('~'),'.platformio','packages','framework-arduinoespressif32','tools','partitions', 'boot_app0.bin')
            shutil.copy(bootappsrc, buildDir)

            addImage(mcu + '-' + envName + '-' + flashsize + '-filesystem', '0x3d0000', 'littlefs.bin', buildDir, mcu + '/' + envName + '/' + flashsize)
            addImage(mcu + '-' + flashsize + '-partitions', '0x8000', 'partitions.bin', buildDir, mcu + '/' + flashsize)
            addImage(mcu + '-bootloader', '0x1000', 'bootloader.bin', buildDir, mcu)
            addImage(mcu + '-bootapp', '0xe000', 'boot_app0.bin', buildDir, mcu)

def addSection(node, name, description, choice):
    section = {
        "name": name,
        "description": description,
    }
    if choice != None:
        section['choice-name'] = choice
        section['choices'] = []
    node.append(section)

def addMCU(name, description, choice=None):
    addSection(manifest['installable']['choices'], name, description, choice)

def addVariant(variant, description, choice=None):
    node1 = manifest['installable']['choices']
    node1len = len(node1)
    addSection(node1[node1len-1]['choices'], variant, description, choice)

def addInstallable(install_type, erase, images):
    for image in images:
        if manifest['images'].get(image) == None:
            # imagefiles = [obj for obj in manifest['images'] if obj['name'] == image]
            # if len(imagefiles) == 0:
            print("Missing image", image)
            sys.exit(1)
        # if len(imagefiles) > 1:
        #    print("Duplicate image", image)
        #    sys.exit(2)
                      
    node1 = manifest['installable']['choices']
    node1len = len(node1)
    node2 = node1[node1len-1]['choices']
    node2len = len(node2)
    installable = {
        "name": install_type["name"],
        "description": install_type["description"],
        "erase": erase,
        "images": images
    }
    node2[node2len-1]['choices'].append(installable)

def addUpload(name, description, files):
    for file in files:
        if manifest['files'].get(file) == None:
            print("Missing file", file)
            sys.exit(1)
    upload = {
        "name": name,
        "description": description,
        "files": files
    }
    manifest['upload']['choices'].append(upload)

fresh_install = { "name": "fresh-install", "description": "Complete FluidNC installation, erasing all previous data"}
firmware_update = { "name": "firmware-update", "description": "Update FluidNC to latest firmware version, preserving previous filesystem data."}
filesystem_update = { "name": "filesystem-update", "description": "Update FluidNC filesystem only, erasing previous filesystem data."}

def makeManifest():
    addMCU("esp32", "ESP32-WROOM", "Firmware variant")

    addVariant("wifi", "Supports WiFi and WebUI", "Installation type")
    addInstallable(fresh_install, True, ["esp32-4m-partitions", "esp32-bootloader", "esp32-bootapp", "esp32-wifi-firmware", "esp32-wifi-4m-filesystem"])
    addInstallable(firmware_update, False, ["esp32-wifi-firmware"])

    addVariant("bt", "Supports Bluetooth serial", "Installation type")
    addInstallable(fresh_install, True, ["esp32-4m-partitions", "esp32-bootloader", "esp32-bootapp", "esp32-bt-firmware"])
    addInstallable(firmware_update, False, ["esp32-bt-firmware"])

    addVariant("noradio", "Supports neither WiFi nor Bluetooth", "Installation type")
    addInstallable(fresh_install, True, ["esp32-4m-partitions", "esp32-bootloader", "esp32-bootapp", "esp32-noradio-firmware"])
    addInstallable(firmware_update, False, ["esp32-noradio-firmware"])

    addFile("WebUI-2", "/localfs/index.html.gz", "index-webui-2.html.gz", os.path.join("release", "current", "data"), "data")
    addFile("WebUI-3", "/localfs/index.html.gz", "index-webui-3.html.gz", os.path.join("release", "current", "data"), "data")

    addUpload("WebUI generation 2", "Add WebUI to local filesystem", ["WebUI-2"])
    addUpload("WebUI generation 3", "Add WebUI to local filesystem", ["WebUI-3"])

makeManifest()

import json
def printManifest():
    print(json.dumps(manifest, indent=2))

with open(os.path.join(manifestRelPath, "manifest.json"), "w") as manifest_file:
    json.dump(manifest, manifest_file, indent=2)
                 

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

        # Put boot_app binary in the archive
        bootapp = 'boot_app0.bin';
        tools = os.path.join(os.path.expanduser('~'),'.platformio','packages','framework-arduinoespressif32','tools')
        zipObj.write(os.path.join(tools, "partitions", bootapp), os.path.join(zipDirName, 'common', bootapp))
        for secFuses in ['SecurityFusesOK.bin', 'SecurityFusesOK0.bin']:
            zipObj.write(os.path.join(sharedPath, 'common', secFuses), os.path.join(zipDirName, 'common', secFuses))

        # Put FluidNC binaries, partition maps, and installers in the archive
        for envName in ['wifi','bt']:

            # Put bootloader binaries in the archive
            bootloader = 'bootloader.bin'
            zipObj.write(os.path.join(pioPath, envName, bootloader), os.path.join(zipDirName, envName, bootloader))

            # Put littlefs.bin and index.html.gz in the archive
            # bt does not need a littlefs.bin because there is no use for index.html.gz
            if envName == 'wifi':
                name = 'littlefs.bin'
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
