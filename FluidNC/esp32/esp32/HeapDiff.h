// Copyright (c) 2026 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Heap block snapshot / diff, for the SmallSD leak hunt.
//
//   $Heap/Snapshot     walk all MALLOC_CAP_8BIT used blocks, record their
//                      addresses into a static baseline buffer.
//   $Heap/Diff         walk again, compare against the baseline, and print the
//                      blocks that were added / freed since the snap.  For added
//                      blocks it dumps the first bytes of live contents.
//   $Heap/Refs=<addr>[,span]
//                      walk all used blocks and print every one that contains a
//                      pointer landing in [addr, addr+span) (span defaults to 4).
//                      Use it to map what references a suspect block.
//
// Only compiled when -DHEAPDIFF is set.

#pragma once

#ifdef HEAPDIFF

#    include "Error.h"
#    include "WebUI/Authentication.h"  // AuthenticationLevel

class Channel;

Error heap_snapshot(const char* value, AuthenticationLevel auth_level, Channel& out);
Error heap_diff(const char* value, AuthenticationLevel auth_level, Channel& out);
Error heap_refs(const char* value, AuthenticationLevel auth_level, Channel& out);

#endif
