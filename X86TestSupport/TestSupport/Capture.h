#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>

// Capture here defines everything that we want to know. Specifically, we want to capture per ID:
// 1. Timings. *When* did something happen?
// 2. Data. This can be a simple '1' or '0', or a character stream. For simplicity, we store a vector of integers.
//
// An ID itself is a string. This can be a pin ID (gpio.1), an uart (uart.0), an ledc, or whatever.

struct CaptureEvent {
    uint32_t              time = 0;
    std::string           id;
    std::vector<uint32_t> data;
};

class Capture {
    Capture() = default;

    std::vector<CaptureEvent> events;
    volatile uint32_t         currentTime = 0;

public:
    static Capture& instance() {
        static Capture instance;
        return instance;
    }

    void reset() { events.clear(); }

    void write(const std::string& id, uint32_t value) {
        CaptureEvent evt;
        evt.time = currentTime;
        evt.id   = id;
        evt.data.reserve(1);
        evt.data.push_back(value);
        events.push_back(evt);
    }

    void write(const std::string& id, uint32_t value, uint32_t time) {
        CaptureEvent evt;
        evt.time = time;
        evt.id   = id;
        evt.data.reserve(1);
        evt.data.push_back(value);
        events.push_back(evt);
    }

    void write(const std::string& id, uint32_t value, std::vector<uint32_t> data) {
        CaptureEvent evt;
        evt.time = currentTime;
        evt.id   = id;
        evt.data = data;
        events.push_back(evt);
    }

    void write(const std::string& id, uint32_t value, uint32_t time, std::vector<uint32_t> data) {
        CaptureEvent evt;
        evt.time = time;
        evt.id   = id;
        evt.data = data;
        events.push_back(evt);
    }

    uint32_t current() { return currentTime; }
    void     wait(uint32_t delay) { currentTime += delay; }
    void     waitUntil(uint32_t value) {
        if (value > currentTime) {
            currentTime = value;
        }
    }
    void yield() { wait(1); }
};

class Inputs {
    std::unordered_map<std::string, std::vector<uint32_t>> data_;

public:
    static Inputs& instance() {
        static Inputs instance;
        return instance;
    }

    void reset() { data_.clear(); }

    void set(const std::string& id, uint32_t value) {
        auto it  = data_.find(id);
        auto vec = std::vector<uint32_t> { value };
        if (it == data_.end()) {
            data_.insert(std::make_pair(id, vec));
        } else {
            it->second = vec;
        }
    }

    void set(const std::string& id, const std::vector<uint32_t>& value) {
        auto it = data_.find(id);
        if (it == data_.end()) {
            data_.insert(std::make_pair(id, value));
        } else {
            it->second = value;
        }
    }

    const std::vector<uint32_t>& get(const std::string& key) {
        static std::vector<uint32_t> empty;

        auto it = data_.find(key);
        if (it == data_.end()) {
            return empty;
        } else {
            return it->second;
        }
    }
};
