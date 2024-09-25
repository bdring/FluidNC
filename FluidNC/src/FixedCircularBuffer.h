// Copyright (c) 2024 - Dylan Knutson <dymk@dymk.co>
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <vector>
#include <optional>

/**
 * A fixed-size circular buffer that stores elements of type T.
 * Keeps track of how many elements have been pushed onto it, and allows
 * for indexing as if it was an infinite sized array. If indexing into
 * the buffer would result in an out-of-bounds access, returns std::nullopt.
 *
 * This is useful for implementing "scrollback" of a buffer of e.g. user
 * provided commands, without using an unbounded amount of memory.
 */
template <typename T>
class FixedCircularBuffer {
public:
    std::vector<T> storage;
    std::size_t    head_idx, tail_idx;

public:
    FixedCircularBuffer() : FixedCircularBuffer(0) {}
    FixedCircularBuffer(size_t size) : storage(size), head_idx(0), tail_idx(0) {}

    /**
     * Push an element onto the end of the buffer.
     */
    void push(T&& elem) {
        storage[tail_idx % storage.size()] = std::move(elem);
        tail_idx += 1;
        if (tail_idx - head_idx > storage.size()) {
            head_idx += 1;
        }
    }

    /**
     * Get the element at the given index, or std::nullopt if the index is out of bounds.
     */
    std::optional<T> at(std::size_t idx) const {
        if (idx >= tail_idx) {
            return std::nullopt;
        }
        if (idx < head_idx) {
            return std::nullopt;
        }
        return storage[idx % storage.size()];
    }

    /**
     * Is the buffer empty?
     */
    bool is_empty() const { return head_idx == tail_idx; }

    /**
     * Get the index of the last element pushed onto the buffer.
     */
    std::size_t position() const { return tail_idx; }
};
