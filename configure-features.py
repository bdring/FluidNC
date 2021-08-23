#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright (C) 2020 Michiyasu Odaki
# Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

# This is useful for automated testing, to make sure you haven't broken something

from __future__ import print_function
import os, sys, argparse, re

configDirName = r'FluidNC/src'
configFileName = r'Config.h'

validFeatureList = [
    'BLUETOOTH',
    'WIFI',
    'SD_CARD',
    'HTTP',
    'OTA',
    'TELNET',
    'TELNET_WELCOME_MSG',
    'MDNS',
    'SSDP',
    'NOTIFICATIONS',
    'SERIAL2SOCKET_IN',
    'SERIAL2SOCKET_OUT',
    'CAPTIVE_PORTAL',
    'AUTHENTICATION'
    ]
eyecatchBeginString = 'CONFIGURE_EYECATCH_BEGIN'
eyecatchEndString = 'CONFIGURE_EYECATCH_END'
enablePrefix = 'ENABLE_'

def printValidFeatureList():
    print("valid feature names:")
    for n in sorted(validFeatureList):
        print("  " + n)

def usage(parser):
    parser.print_usage()
    printValidFeatureList()
    sys.exit(255)

def parseArgs(defaultConfigPath):
    # Argument parser (-h is also available)
    parser = argparse.ArgumentParser(description='Configure FluidNC features')
    parser.add_argument('-v', '--verbose', action='store_true')
    parser.add_argument('-c', '--configfile', default=defaultConfigPath)
    parser.add_argument('-e', '--enable', nargs='*'
                        ,help='specify feature name(ex. WIFI)'
                        #, choices=validFeatureList
                        )
    parser.add_argument('-d', '--disable', nargs='*'
                        ,help='specify feature name(ex. BLUETOOTH)'
                        #,choices=validFeatureList
                        )
    return parser.parse_args(), parser

def isValidFeature(feature):
    if feature:
        if feature in validFeatureList:
            return True #valid
    return False #invalid

def checkFeatureList(optname, features, verbose=False):
    if features:
        for n in features:
            if isValidFeature(n) is True:
                #if verbose:
                #    print("valid " + optname + " feature: " + n)
                pass
            else:
                print("unknown " + optname + " feature: " + n)
                return -1
    else:
        #if verbose:
        #    print(optname + " is not specified")
        return 0

    return len(features)

def resolveConflicts(high_priority, low_priority):
    if low_priority:
        for n in low_priority[:]:
            if n in high_priority:
                low_priority.remove(n)
    return

def readConfig(path):
    # Read config.h to memory
    try:
        f = open(path)
    except IOError:
        print("unable to open file for read: " + path)
        sys.exit(255)
    else:
        try:
            src = f.readlines()
        except IOError:
            print("file read error: " + path)
            f.close()
            sys.exit(255)
        else:
            f.close()
    return src

def writeConfig(path, buf):
    try:
        f = open(path, 'w')
    except IOError:
        print("unable to open file for write: " + path)
        sys.exit(255)
    else:
        try:
            src = f.write(buf)
        except IOError:
            print("file write error: " + path)
            f.close()
            sys.exit(255)
        else:
            f.close()

def changeConfig(src, enabled, disabled, verbose=False):
    dst = ""
    numEnabledDic = {}
    for name in enabled:
        numEnabledDic[name] = 0
    regstart = re.compile(r'^\s*//\s*' + eyecatchBeginString)
    regend = re.compile(r'^\s*//\s*' + eyecatchEndString)
    state = 0;
    for line in src:
        if state == 0:
            # out of the defines block to modify
            if regstart.match(line):
                state = 1
            dst += line
        elif state == 1:
            # in the defines block to modify
            if regend.match(line):
                # end of block found
                for name in enabled:
                    # add the define to enable if it was not found in the block
                    if numEnabledDic[name] == 0:
                        dst += "#define " + enablePrefix + name + '\n'
                state = 2
                dst += line
            else:
                dstLine = ""
                if len(enabled) > 0:
                    for name in enabled:
                        s = enablePrefix + name
                        m = re.match(r'^s*(//)*\s*#define\s+(' + s + r'.*)$', line)
                        if m:
                            dstLine = "#define " + m.group(2) + '\n'
                            numEnabledDic[name] += 1
                            break
                if len(disabled) > 0:
                    for name in disabled:
                        s = enablePrefix + name
                        m = re.match(r'^\s*#define\s+' + s + r'.*$', line)
                        if m:
                            dstLine = "//" + line
                            break
                if len(dstLine) != 0:
                    dst += dstLine
                    if verbose:
                        print(dstLine.rstrip())
                else:
                    dst += line
                    if verbose:
                        print(line.rstrip())
        elif state == 2:
            dst += line
    if state != 2:
        print("EYECATCH pair not found. Cancel changes.")
        sys.exit(255)
    return dst

def main():
    # Concatinate dir and file
    configFilePath = os.path.join(configDirName, configFileName)

    # Parse arguments
    args, parser = parseArgs(configFilePath)

    # Set params to local variables
    verbose = args.verbose
    configFilePath = args.configfile

    # Copy and remove duplicates from enable/disable list
    enabledFeatures = []
    disabledFeatures = []
    if args.enable:
        enabledFeatures = sorted(set(args.enable), key=args.enable.index)
    if args.disable:
        disabledFeatures = sorted(set(args.disable), key=args.disable.index)

    # If both enable and disable are specified, treat as enable
    # (Remove items from 2nd param if same item in 1st param)
    resolveConflicts(enabledFeatures, disabledFeatures)

    # Verification (whether or not to use "choice" with argparse)
    numEnables = checkFeatureList("-e", enabledFeatures, verbose)
    numDisables = checkFeatureList("-d", disabledFeatures, verbose)
    #if verbose:
    #    print("enables: ", numEnables)
    #    print(enabledFeatures)
    #    print("disables: ", numDisables)
    #    print(disabledFeatures)
    #    print("config file: " + configFilePath)

    # Final checking about args
    if numEnables < 0 or numDisables < 0 or (numEnables == 0 and numDisables == 0):
        usage(parser)

    # Check if the target file exists
    configFilePathAbs = os.path.abspath(configFilePath)
    if verbose:
        print("Config path: " + configFilePathAbs)
        print()
    if not os.path.isfile(configFilePathAbs):
        print("config file not found: " + configFilePathAbs)
        sys.exit(255)

    # read config file
    src = readConfig(configFilePathAbs)

    # Change the specified settings
    dst = changeConfig(src, enabledFeatures, disabledFeatures, verbose)

    # Write back
    writeConfig(configFilePathAbs, dst)

    sys.exit(0)

if __name__ == "__main__":
    main()
