# InputFile.cpp Data Loss Analysis: 128-Byte Boundary Issues

## Overview
Investigation of potential data loss in `InputFile` class when reading from files, particularly at 128-byte boundaries. The issue affects the file-based G-code execution pipeline.

## Class Hierarchy & Data Flow

```
Channel (base)
  └─ FileStream (inherits from Channel)
      └─ InputFile (inherits from FileStream)
```

### Critical Path
```
InputFile::readLine() 
  → loop: read() [single byte at a time]
    → FileStream::read() [single byte version]
      → fread(buffer, 1, 1, _fd) [C stdlib]
        → VFS layer (ftell() used for position tracking)
          → LittleFS
```

## Code Analysis

### 1. InputFile::readLine() - Core Reading Loop

**File**: `FluidNC/src/InputFile.cpp` (lines 13-32)

```cpp
Error InputFile::readLine(char* line, size_t maxlen) {
    size_t len = 0;
    int    c;
    while ((c = read()) >= 0) {  // ← Single-byte reads in loop
        if (len >= maxlen) {
            return Error::LineLengthExceeded;
        }
        if (c == '\r') {
            continue;  // ← Skips carriage returns
        }
        if (c == '\n') {
            ++_line_number;
            if (len == 0) {
                ++_blank_lines;
            }
            break;
        }
        line[len++] = c;
    }
    line[len] = '\0';
    return len || c >= 0 ? Error::Ok : Error::Eof;
}
```

**Problem Indicators**:
- Calls `read()` **character by character**
- C stdio buffering typically allocates **128-256 bytes** internally
- Buffer boundary crossing could expose state inconsistencies

### 2. FileStream::read() - Buffer Access

**File**: `FluidNC/src/FileStream.cpp` (lines 29-30)

```cpp
int FileStream::read(char* buffer, size_t length) {
    return fread(buffer, 1, length, _fd);  // ← Direct fread call
}
```

**Single-byte implementation** (from Stream base):
```cpp
int read() {  // Inherited from Stream
    // This internally calls read(buffer, 1)
    // which becomes fread(buffer, 1, 1, _fd)
}
```

### 3. FileStream::position() - Position Tracking

**File**: `FluidNC/src/FileStream.cpp` (lines 51-52)

```cpp
size_t FileStream::position() {
    return ftell(_fd);  // ← Critical dependency on ftell()
}
```

### 4. InputFile::pollLine() - Higher-Level Interface

**File**: `FluidNC/src/InputFile.cpp` (lines 72-102)

Uses `position()` for progress calculation:
```cpp
float percent_complete = ((float)position()) * 100.0f / size();
```

**Vulnerability**: If `position()` is incorrect at 128-byte boundaries, progress tracking gets corrupted.

---

## Suspected Root Causes

### A. C Runtime Buffering Issue

The C standard library uses **buffered I/O**:
- Default buffer size: typically 128-256 bytes on embedded systems
- When `fread(buf, 1, 1, fd)` is called repeatedly:
  1. First call reads 128 bytes into internal buffer, returns byte[0]
  2. Calls 2-127 return from buffer without I/O
  3. Call 128 refills buffer from file...
  4. **Problem**: If position tracking or buffer state is corrupt at refill point, byte 128 could be lost or duplicated

### B. VFS ftell() Bug Continuation

We fixed `_lseek()` in the RP2040 VFS layer to return correct absolute position. **However**, there may be related issues:

1. **ftell() implementation**: May not properly track byte position across fread() calls
2. **Buffer coherency**: fread() uses internal buffering; ftell() queries file position; these must stay synchronized
3. **Interaction with LittleFS**: LittleFS has 4KB blocks; 128-byte boundaries don't align, but buffer boundaries might cause issues

### C. Mixing Single-Byte and Bulk Operations

FileStream supports:
- `read(char*, size_t)` - bulk read via fread()
- `read()` - single-byte read via fread()

If the C runtime's buffer isn't properly flushed/managed between these, data could be lost at buffer boundaries.

### D. No Buffer Management

**Current code does NOT call**:
- `setvbuf()` - to control buffer size/mode
- `fflush()` - to synchronize buffers with file state
- `flushRx()` - Channel method (Overridden but empty in FileStream)

This leaves buffering to default behavior, which may be problematic at boundaries.

---

## 128-Byte Significance

The 128-byte boundary is likely:
1. **TelnetClient buffer size** (line 35 in WebUI/TelnetClient.cpp):
   ```cpp
   const int bufsize = 128;
   ```
2. **Default C stdlib buffer size** on RP2040/ESP32
3. **VFS internal buffer size** (speculation - needs verification)

When file operations cross buffer boundaries (at bytes 128, 256, 384, ...), the buffer refill code path executes. If there's a state bug there, data loss occurs.

---

## Risk Assessment

| Component | Risk | Evidence |
|-----------|------|----------|
| **fread() buffering** | HIGH | Character-by-character reads trigger buffer refills at multiples of 128 |
| **ftell() accuracy** | HIGH | Previous VFS._lseek() bug suggests position tracking issues exist |
| **Buffer coherency** | MEDIUM | No explicit setvbuf() or buffer synchronization |
| **VFS implementation** | MEDIUM | RP2040 VFS has known bugs (we fixed one); others may exist |
| **LittleFS interaction** | LOW | LittleFS uses 4KB blocks; unlikely to affect 128-byte boundaries directly |

---

## Recommended Investigation Steps

### 1. Add Buffer Diagnostics
Instrument `FileStream::read()` and add logging:
```cpp
int FileStream::read(char* buffer, size_t length) {
    size_t pos_before = position();
    int result = fread(buffer, 1, length, _fd);
    size_t pos_after = position();
    
    // Log at 128-byte boundaries
    if ((pos_before % 128) != (pos_after % 128)) {
        log_debug("Buffer boundary crossed: %u → %u (read %d bytes)", 
                  pos_before, pos_after, result);
    }
    return result;
}
```

### 2. Force Full-Buffering Mode
In `FileStream::setup()`:
```cpp
void FileStream::setup(const char* mode) {
    _fd = fopen(_fpath.string().c_str(), mode);
    if (!_fd) { /* error handling */ }
    // Force full buffering with known size
    set_buffer(NULL, _IOFBF, 4096);  // 4KB buffer (LittleFS block size)
    _size = stdfs::file_size(_fpath);
}
```

### 3. Verify ftell() at Boundaries
In `InputFile::readLine()`:
```cpp
Error InputFile::readLine(char* line, size_t maxlen) {
    size_t len = 0;
    int c;
    size_t line_start_pos = position();  // Capture starting position
    
    while ((c = read()) >= 0) {
        size_t expected_pos = line_start_pos + len + 1;  // +1 for current char
        size_t actual_pos = position();
        
        if (actual_pos != expected_pos) {
            log_warn("Position mismatch at byte %u: expected %u, got %u",
                     line_start_pos + len, expected_pos, actual_pos);
        }
        
        // ... rest of loop ...
    }
    // ...
}
```

### 4. Test with Boundary-Aligned Input
Create test file with known pattern:
- 127 bytes of 'A'
- 128 bytes of 'B' (crosses boundary)
- 129 bytes of 'C'
- Verify all bytes are read correctly

### 5. Check VFS Layer Buffer Management
Review RP2040 VFS implementation for:
- `_read()` function state at buffer boundaries
- `_lseek()` interaction with read buffers (may need flushing)
- Proper buffer synchronization on concurrent access

---

## Potential Fixes

### Fix 1: Disable Buffering (Safest for single-byte reads)
```cpp
void FileStream::setup(const char* mode) {
    _fd = fopen(_fpath.string().c_str(), mode);
    if (!_fd) { /* error */ }
    setvbuf(_fd, nullptr, _IONBF, 0);  // No buffering
    _size = stdfs::file_size(_fpath);
}
```
**Pros**: Eliminates buffer boundary issues  
**Cons**: Performance degradation (more system calls)

### Fix 2: Use Bulk Read with Internal Buffering
Modify `InputFile::readLine()` to read in chunks:
```cpp
static constexpr size_t READ_BUFFER_SIZE = 256;

Error InputFile::readLine(char* line, size_t maxlen) {
    static char read_buffer[READ_BUFFER_SIZE];
    static size_t buffer_pos = 0;
    static size_t buffer_len = 0;
    
    size_t line_len = 0;
    
    while (line_len < maxlen) {
        // Refill buffer when empty
        if (buffer_pos >= buffer_len) {
            buffer_len = read(read_buffer, READ_BUFFER_SIZE);
            buffer_pos = 0;
            if (buffer_len == 0) {
                break;  // EOF
            }
        }
        
        char c = read_buffer[buffer_pos++];
        if (c == '\r') continue;
        if (c == '\n') {
            ++_line_number;
            break;
        }
        line[line_len++] = c;
    }
    
    line[line_len] = '\0';
    return line_len || buffer_len > 0 ? Error::Ok : Error::Eof;
}
```
**Pros**: Maintains single-byte interface; reduces system calls  
**Cons**: More complex; needs static state management

### Fix 3: Verify and Fix VFS ftell()
Ensure RP2040 VFS `_lseek()` correctly updates internal position:
```cpp
// In RP2040 VFS layer
static long vfs_lseek(void* ctx, int fd, long offset, int whence) {
    auto file = &files[fd];
    
    switch (whence) {
        case SEEK_CUR:
            // Bug: This used to return 0/1 instead of absolute position
            if (offset == 0) {
                return file->position();  // ← MUST return absolute
            }
            offset += file->position();
            // Fall through
        case SEEK_SET:
            file->position(offset);
            return file->position();  // ← Return absolute position
        case SEEK_END:
            offset += file->size();
            file->position(offset);
            return file->position();
        default:
            return -1;
    }
}
```

---

## Summary

The 128-byte boundary data loss is likely caused by **interaction between**:
1. Single-byte `fread()` calls triggering C runtime buffer refills
2. Partial or incorrect position tracking via `ftell()`
3. Possible VFS layer buffering bugs similar to the `_lseek()` bug we fixed

**Immediate Action**: Add instrumentation at buffer boundaries to confirm the hypothesis, then implement Fix #1 or #2 depending on performance requirements.

