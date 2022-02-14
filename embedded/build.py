# Creates a NoFile.h include file that contains the embedded
# web page that is used when index.html.gz is missing
# NoFile.h is created from the code in <FluidNC>/embedded

import subprocess, sys

header = """// Embedded web page to load the index,html.gz for ESP3D-WEBUI.
// Generated from the code in <FluidNC>/embedded/ .  Do not edit manually.

// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// License: GPL version 2.1 or (at your option) any later version.

#pragma once
// clang-format off

"""

footer = ""

def bin2header(data, var_name='var'):
    out = []
    out.append('const char {var_name}[] = {{'.format(var_name=var_name))
    l = [ data[i:i+16] for i in range(0, len(data), 16) ]
    for i, x in enumerate(l):
        line = ', '.join([   '0x{val:02X}'.format(val=c) for c in x ])
        out.append('  {line}{end_comma}'.format(line=line, end_comma=',' if i<len(l)-1 else ''))
    out.append('};')
    out.append('unsigned int {var_name}_SIZE = {data_len};'.format(var_name=var_name, data_len=len(data)))
    return '\n'.join(out) + '\n'

subprocess.run(["npm", "install"])
subprocess.run(["npm", "audit", "fix"])
subprocess.run(["gulp", "package"])

with open('tool.html.gz', 'rb') as f:
    data = f.read()

out = bin2header(data, 'PAGE_NOFILES')
with open('../FluidNC/src/WebUI/NoFile.h', 'wb') as f:
    f.write(bytearray(header, 'latin1'))
    f.write(bytearray(out, 'latin1'))
    f.write(bytearray(footer, 'latin1'))
