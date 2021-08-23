#!/usr/bin/env python

# Compile FluidNC for each of the machines defined in Machines/ .
# Add-on files are built on top of a single base.
# This is useful for automated testing, to make sure you haven't broken something

# The output is filtered so that the only lines you see are a single
# success or failure line for each build, plus any preceding lines that
# contain the word "error".  If you need to see everything, for example to
# see the details of an errored build, include -v on the command line.

from shutil import copy
from builder import buildMachine
import os, sys

cmd = ['platformio','run']
builds = ['wifi','bt','wifibt','noradio']

verbose = '-v' in sys.argv

if not os.path.exists('release'):
    os.makedirs('release')

numErrors = 0
for name in builds:
    exitCode = buildMachine(name, verbose=verbose)
    if exitCode != 0:
        numErrors += 1
    else:
        firmwareelf = os.path.join('.pio', 'build', name, 'firmware.elf');
        firmwarebin = os.path.join('.pio', 'build', name, 'firmware.bin');
        targetelf = os.path.join('release', name + '.elf');
        targetbin = os.path.join('release', name + '.bin');
        copy(firmwareelf, targetelf)
        copy(firmwarebin, targetbin)

sys.exit(numErrors)
