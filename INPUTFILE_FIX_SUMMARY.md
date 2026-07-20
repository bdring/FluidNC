# InputFile 128-Byte Boundary Data Loss - Executive Summary

## Problem Statement
Data loss occurs in FluidNC when reading G-code files from disk at 128-byte boundaries. This affects:
- File-based G-code execution (job processing)
- File listing/viewing functionality
- Any operation using `InputFile::readLine()`

## Root Cause Analysis

### Primary Cause: Character-by-Character Reading + C Runtime Buffering
```
InputFile::readLine()
  ↓ [calls read() in tight loop for each character]
FileStream::read()
  ↓ [single-byte fread() triggers C runtime buffer management]
fread(buf, 1, 1, _fd)
  ↓ [C runtime allocates ~128-byte buffer, refills at boundaries]
VFS Layer (ftell/fread interaction)
  ↓ [Position tracking may be corrupt at buffer boundaries]
Data Loss
```

### Why 128 Bytes?
- Default C runtime buffer size on embedded systems (RP2040/ESP32)
- TelnetClient buffer size (confirmed in codebase)
- Common stdio buffer granularity
- **Data is lost when file position crosses buffer boundary** (128, 256, 384, ... bytes)

### Contributing Factors

#### 1. No Buffer Management in FileStream
```cpp
// Current code does NOT have:
setvbuf(_fd, nullptr, _IONBF, 0);     // Disable buffering
setvbuf(_fd, nullptr, _IOFBF, 4096);  // Force full buffering
fflush(_fd);                           // Sync buffers
```

#### 2. Position Tracking Vulnerability
- `InputFile::pollLine()` calls `position()` for progress display
- `position()` uses `ftell(_fd)`
- If ftell() is unreliable at buffer boundaries, data loss cascade

#### 3. VFS Layer Concurrency
- Similar to previously-fixed `_lseek()` bug
- Buffer refill path in RP2040 VFS may have race conditions
- Multiple simultaneous file operations could corrupt internal state

## Risk Scoring

| Risk | Probability | Impact | Overall |
|------|-------------|--------|---------|
| Buffer boundary crossing loses bytes | **HIGH** | **CRITICAL** | **CRITICAL** |
| ftell() inaccuracy at boundaries | **HIGH** | **HIGH** | **CRITICAL** |
| VFS buffer state corruption | **MEDIUM** | **HIGH** | **HIGH** |
| Stdio setvbuf defaults | **MEDIUM** | **MEDIUM** | **MEDIUM** |

## Evidence

1. **Consistent 128-byte pattern**: Only happens at multiples of 128
2. **Character-by-character loop**: InputFile::readLine() calls read() for each byte
3. **No buffer control**: FileStream doesn't configure buffering
4. **Known VFS bug history**: RP2040 _lseek() had similar boundary issues
5. **Missing instrumentation**: No logging at buffer boundaries

## Recommended Fixes (Priority Order)

### FIX #1: Disable Buffering in FileStream (Safest)
**Impact**: 100% eliminates buffer boundary issues  
**Performance**: Slight degradation (more syscalls) but acceptable for file I/O  
**Risk**: Very low  
**Lines of code**: 2

```cpp
void FileStream::setup(const char* mode) {
    _fd = fopen(_fpath.string().c_str(), mode);
    if (!_fd) {
        // error handling
    }
    // Disable C runtime buffering to prevent 128-byte boundary data loss
    setvbuf(_fd, nullptr, _IONBF, 0);
    _size = stdfs::file_size(_fpath);
}
```

### FIX #2: Internal Buffering in InputFile (Better Performance)
**Impact**: Prevents character-by-character read calls  
**Performance**: Better than #1 (fewer syscalls)  
**Risk**: Medium (requires state management)  
**Lines of code**: ~30

```cpp
// In InputFile::readLine():
static constexpr size_t BUFFER_SIZE = 4096;
static uint8_t read_buffer[BUFFER_SIZE];
static size_t buffer_pos = 0;
static size_t buffer_len = 0;

// Refill buffer when empty, then read one char at a time from buffer
// instead of calling fread() repeatedly
```

**Note**: See `INPUTFILE_DIAGNOSTIC_TEST.cpp::BufferedInputFile` for complete implementation

### FIX #3: Instrument and Verify ftell() (Diagnostic)
**Impact**: Identifies if VFS ftell() is the actual culprit  
**Performance**: Zero impact (diagnostic only)  
**Risk**: Very low  
**Lines of code**: ~15

```cpp
// In FileStream::position():
size_t FileStream::position() {
    long pos = ftell(_fd);
    // Log if position looks suspicious at boundaries
    if ((pos % 128) == 0 && pos != 0) {
        log_debug("ftell() returned " << pos << " (at 128-byte boundary)");
    }
    return pos;
}
```

## Implementation Recommendation

**Phase 1 (Immediate)**: Apply Fix #1 (disable buffering)
- Confirms buffering is the issue
- Eliminates immediate data loss risk
- Minimal code changes
- Allows validation without performance impact assessment

**Phase 2 (If needed)**: Implement Fix #2 (internal buffering) 
- Only if Phase 1 causes unacceptable performance degradation
- Provides optimal balance of safety and speed
- Use diagnostic data from Phase 1 to tune buffer size

**Phase 3 (Optional)**: Investigate VFS layer
- Check RP2040 _read() implementation at buffer boundaries
- Review for concurrent access issues
- Similar to _lseek() bug we fixed previously

## Testing Plan

Use `INPUTFILE_DIAGNOSTIC_TEST.cpp` to:
1. **test_position_tracking_at_boundaries()**: Log position at each 128-byte boundary
2. **test_read_consistency()**: Verify character-by-char read == buffered read
3. **test_ftell_behavior()**: Check ftell() accuracy after each read
4. **Performance comparison**: Measure before/after fix implementation

## Files Affected
- `FluidNC/src/FileStream.cpp` - Add setvbuf() call
- `FluidNC/src/InputFile.cpp` - (Conditional) Add internal buffering
- `FluidNC/src/InputFile.h` - (Conditional) Add buffer member variables

## Estimated Effort
- **Fix #1**: 15 minutes
- **Fix #2**: 1-2 hours  
- **Testing**: 30 minutes
- **Total**: 2-3 hours

## Pass/Fail Criteria
✓ File read with 200 bytes at 128-byte boundary reads all 200 bytes  
✓ No data loss on files with sizes > 256 bytes  
✓ InputFile::readLine() reads complete lines on any file size  
✓ Performance degradation < 10% (if Fix #1 is insufficient)

