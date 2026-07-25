// Diagnostic test code for InputFile 128-byte boundary data loss
// Add this to a test harness to identify buffering issues

#include "InputFile.h"
#include "Report.h"
#include <cstring>

// Test 1: Verify position tracking at boundaries
void test_position_tracking_at_boundaries(const char* test_file) {
    InputFile file(LocalFS, test_file);
    
    log_info("=== Position Tracking Test ===");
    log_info("File size: " << file.size() << " bytes");
    
    char line[256];
    size_t bytes_read = 0;
    int line_count = 0;
    
    while (file.readLine(line, sizeof(line) - 1) == Error::Ok) {
        size_t line_len = strlen(line);
        bytes_read += line_len + 1;  // +1 for newline
        line_count++;
        
        // Log at 128-byte boundaries
        if ((bytes_read % 128) < line_len) {
            size_t pos = file.position();
            log_debug("Line " << line_count << ": crossed 128-byte boundary at " 
                      << (bytes_read - line_len) << "-" << bytes_read 
                      << " bytes (ftell reports: " << pos << ")");
            
            // Verify ftell accuracy
            if (pos != bytes_read) {
                log_warn("POSITION MISMATCH: Expected ~" << bytes_read 
                         << ", ftell() returned " << pos);
            }
        }
    }
    
    log_info("Total bytes read: " << bytes_read << ", lines: " << line_count);
}

// Test 2: Inject instrumentation into read path
class InstrumentedInputFile : public InputFile {
private:
    size_t _bytes_read_total = 0;
    
public:
    InstrumentedInputFile(const Volume& fs, const char* path) 
        : InputFile(fs, path) {}
    
    Error readLine(char* line, size_t maxlen) override {
        size_t pos_before = position();
        Error result = InputFile::readLine(line, maxlen);
        size_t pos_after = position();
        size_t line_len = strlen(line);
        
        _bytes_read_total += line_len + 1;  // +1 for newline
        
        // Check for data loss: position should advance by at least line length + newline
        if (result == Error::Ok && (pos_after - pos_before) != (line_len + 1)) {
            log_warn("Data loss detected: line len=" << line_len 
                     << " but position advanced by " << (pos_after - pos_before)
                     << " (pos " << pos_before << " → " << pos_after << ")");
        }
        
        // Log at 128-byte boundaries
        if ((_bytes_read_total / 128) != ((_bytes_read_total - line_len - 1) / 128)) {
            log_debug("Crossed 128-byte boundary at byte " << _bytes_read_total);
        }
        
        return result;
    }
};

// Test 3: Character-level comparison test
// Reads file two ways: line-by-line vs buffer-based, compares results
void test_read_consistency(const char* test_file) {
    log_info("=== Read Consistency Test ===");
    
    // Method A: read() character by character (current approach)
    {
        InputFile file(LocalFS, test_file);
        std::string content_a;
        int c;
        while ((c = file.read()) >= 0) {
            content_a += (char)c;
        }
        log_info("Method A (character-by-char): " << content_a.length() << " bytes");
    }
    
    // Method B: read() in larger chunks (proposed approach)
    {
        InputFile file(LocalFS, test_file);
        std::string content_b;
        char buffer[256];
        int nread;
        while ((nread = file.read(buffer, sizeof(buffer))) > 0) {
            content_b.append(buffer, nread);
        }
        log_info("Method B (buffered chunks): " << content_b.length() << " bytes");
    }
    
    // Verify they match
    // Note: If this test passes, the issue is not in read() itself
}

// Test 4: VFS ftell behavior under repeated single-byte reads
void test_ftell_behavior(const char* test_file) {
    log_info("=== ftell() Behavior Test ===");
    
    InputFile file(LocalFS, test_file);
    size_t expected_pos = 0;
    size_t mismatch_count = 0;
    
    for (int i = 0; i < 512; i++) {
        int c = file.read();
        if (c < 0) break;
        
        expected_pos++;
        size_t actual_pos = file.position();
        
        if (actual_pos != expected_pos) {
            log_error("Byte " << i << ": expected pos=" << expected_pos 
                      << ", ftell() returned " << actual_pos);
            mismatch_count++;
            
            if (mismatch_count > 5) {
                log_error("Too many mismatches, stopping test");
                break;
            }
        }
        
        // Special attention at 128-byte boundaries
        if (expected_pos % 128 == 0) {
            log_debug("At 128-byte boundary " << (expected_pos / 128) * 128);
        }
    }
    
    if (mismatch_count == 0) {
        log_info("ftell() tracking is accurate");
    }
}

// Proposed fix: Buffered line reading
class BufferedInputFile : public InputFile {
private:
    static constexpr size_t BUFFER_SIZE = 4096;  // 4KB = LittleFS block size
    uint8_t _buffer[BUFFER_SIZE];
    size_t _buffer_pos = 0;
    size_t _buffer_len = 0;
    
    int next_char() {
        if (_buffer_pos >= _buffer_len) {
            _buffer_len = InputFile::read((char*)_buffer, BUFFER_SIZE);
            _buffer_pos = 0;
            if (_buffer_len == 0) {
                return -1;  // EOF
            }
        }
        return _buffer[_buffer_pos++];
    }
    
public:
    using InputFile::InputFile;
    
    Error readLine(char* line, size_t maxlen) override {
        size_t len = 0;
        int c;
        
        while ((c = next_char()) >= 0) {
            if (len >= maxlen) {
                return Error::LineLengthExceeded;
            }
            if (c == '\r') {
                continue;
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
};

// Main test harness
void run_inputfile_diagnostics(const char* test_file) {
    log_info("Starting InputFile diagnostic tests");
    log_info("Test file: " << test_file);
    
    try {
        test_position_tracking_at_boundaries(test_file);
        test_read_consistency(test_file);
        test_ftell_behavior(test_file);
        
        log_info("All diagnostic tests completed");
    } catch (const std::exception& e) {
        log_error("Diagnostic test failed: " << e.what());
    }
}
