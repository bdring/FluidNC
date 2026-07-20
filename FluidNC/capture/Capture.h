#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

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
    mutable std::mutex        events_mutex_;
    std::atomic<uint32_t>     currentTime { 0 };

public:
    static Capture& instance() {
        static Capture instance;
        return instance;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(events_mutex_);
        events.clear();
    }

    void write(const std::string& id, uint32_t value) {
        CaptureEvent evt;
        evt.time = currentTime.load(std::memory_order_relaxed);
        evt.id   = id;
        evt.data.reserve(1);
        evt.data.push_back(value);
        std::lock_guard<std::mutex> lock(events_mutex_);
        events.push_back(evt);
    }

    void write(const std::string& id, uint32_t value, uint32_t time) {
        CaptureEvent evt;
        evt.time = time;
        evt.id   = id;
        evt.data.reserve(1);
        evt.data.push_back(value);
        std::lock_guard<std::mutex> lock(events_mutex_);
        events.push_back(evt);
    }

    void write(const std::string& id, uint32_t value, std::vector<uint32_t> data) {
        CaptureEvent evt;
        evt.time = currentTime.load(std::memory_order_relaxed);
        evt.id   = id;
        evt.data = data;
        std::lock_guard<std::mutex> lock(events_mutex_);
        events.push_back(evt);
    }

    void write(const std::string& id, uint32_t value, uint32_t time, std::vector<uint32_t> data) {
        CaptureEvent evt;
        evt.time = time;
        evt.id   = id;
        evt.data = data;
        std::lock_guard<std::mutex> lock(events_mutex_);
        events.push_back(evt);
    }

    uint32_t current() { return currentTime.load(std::memory_order_relaxed); }

    void wait(uint32_t delay) {
        // Actually wait in real time (delay is in milliseconds)
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        currentTime.fetch_add(delay, std::memory_order_relaxed);
    }

    void waitUntil(uint32_t value) {
        auto now = currentTime.load(std::memory_order_relaxed);
        while (value > now) {
            uint32_t delay = value - now;
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            if (currentTime.compare_exchange_weak(now, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
                return;
            }
        }
    }

    void yield() {
        // Yield to other threads with minimal delay
        std::this_thread::yield();
        currentTime.fetch_add(1, std::memory_order_relaxed);
    }
};

class Inputs {
    std::unordered_map<std::string, std::vector<uint32_t>> data_;
    mutable std::mutex                                      mutex_;

public:
    static Inputs& instance() {
        static Inputs instance;
        return instance;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.clear();
    }

    void set(const std::string& id, uint32_t value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it  = data_.find(id);
        auto vec = std::vector<uint32_t> { value };
        if (it == data_.end()) {
            data_.insert(std::make_pair(id, vec));
        } else {
            it->second = vec;
        }
    }

    void set(const std::string& id, const std::vector<uint32_t>& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = data_.find(id);
        if (it == data_.end()) {
            data_.insert(std::make_pair(id, value));
        } else {
            it->second = value;
        }
    }

    size_t size(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = data_.find(key);
        return it == data_.end() ? 0 : it->second.size();
    }

    size_t read(const std::string& key, uint8_t* buffer, size_t length) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = data_.find(key);
        if (it == data_.end() || !length) {
            return 0;
        }

        auto&  value   = it->second;
        size_t copylen = std::min(length, value.size());
        for (size_t i = 0; i < copylen; ++i) {
            buffer[i] = uint8_t(value[i]);
        }
        value.erase(value.begin(), value.begin() + copylen);
        return copylen;
    }

    void append(const std::string& key, const uint8_t* buffer, size_t length) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto&                       value = data_[key];
        value.reserve(value.size() + length);
        for (size_t i = 0; i < length; ++i) {
            value.push_back(uint32_t(buffer[i]));
        }
    }
};
