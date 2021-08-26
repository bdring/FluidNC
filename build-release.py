#!/usr/bin/env python

# Compile FluidNC for each of the radio modes
# Add-on files are built on top of a single base.
# This is useful for automated testing, to make sure you haven't broken something

# The output is filtered so that the only lines you see are a single
# success or failure line for each build, plus any preceding lines that
# contain the word "error".  If you need to see everything, for example to
# see the details of an errored build, include -v on the command line.

from shutil import copy
from builder import buildEnv, buildFs
from zipfile import ZipFile
import subprocess, os, sys

builds = ['wifi','bt','wifibt','noradio']
objects = ['firmware.elf','firmware.bin','spiffs.bin','partitions.bin']

verbose = '-v' in sys.argv

tag = (
    subprocess.check_output(["git", "describe", "--tags", "--abbrev=0"])
    .strip()
    .decode("utf-8")
)
filePrefix = 'fluidnc-' + tag + '-'

relPath = os.path.join('release', tag)
if not os.path.exists(relPath):
    os.makedirs(relPath)

copy('HOWTO-INSTALL-Linux-Mac.txt', relPath)
copy('HOWTO-INSTALL-Windows.txt', relPath)
copy('InstallFluidNC.ps1', relPath)
copy('InstallFluidNC.sh', relPath)

numErrors = 0

pioPath = os.path.join('.pio', 'build')

for envName in builds:
    exitCode = buildEnv(envName, verbose=verbose)
    if exitCode != 0:
        numErrors += 1
    else:
        exitCode = buildFs(envName, verbose=verbose)
        if exitCode != 0:
            numErrors += 1
        else:
            objPath = os.path.join(pioPath, envName)
            zipFileName = os.path.join(relPath, filePrefix + envName + '.zip')
            with ZipFile(zipFileName, 'w') as zipObj:
                for obj in objects:
                    objFile = os.path.join(objPath, obj)
                    zipObj.write(objFile, obj)

sys.exit(numErrors)
