#!/usr/bin/env python

# Compile FluidNC for each of the machines defined in Machines/ .
# Add-on files are built on top of a single base.
# This is useful for automated testing, to make sure you haven't broken something

# The output is filtered so that the only lines you see are a single
# success or failure line for each build, plus any preceding lines that
# contain the word "error".  If you need to see everything, for example to
# see the details of an errored build, include -v on the command line.

from __future__ import print_function
from builder import buildMachine
import os, sys

extraArgs=None

verbose = '-v' in sys.argv or '-q' not in sys.argv
if '-v' in sys.argv:
    sys.argv.remove('-v')
if '-q' in sys.argv:
    sys.argv.remove('-q')
if '-u' in sys.argv:
    sys.argv.remove('-u')
    extraArgs = '--target=upload'

exitCode = 255
if len(sys.argv) == 2:
    exitCode = buildMachine(sys.argv[1], verbose=verbose, extraArgs=extraArgs)
else:
    print("Usage: ./build-machine.py [-q] [-u] machine_name.h")
    print(' Build for the given machine regardless of machine.h')
    print('  -q suppresses most messages')
    print('  -u uploads to the target after compilation')

sys.exit(exitCode)
