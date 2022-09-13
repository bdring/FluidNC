// Copyright (c) 2022 -	Jonathan Heinen
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Logging.h"

template <typename T>
struct Entry {
    const char* name;
    T*          value;
};

template <typename T>
class ExternalList {
private:
    Entry<T> entries[10] { nullptr };
    int      size = 0;

public:
    void add(const char* name, T*& value) {
        entries[size]       = Entry<T>();
        entries[size].name  = name;
        entries[size].value = value;
        size++;
    }

    void get(const char* name, T*& value) {
        for (int i = 0; i < size; i++) {
            if (strcmp(entries[i].name, name)) {
                value = entries[i].value;
            }
        }
    }

    const char* getName(T* value) {
        for (int i = 0; i < size; i++) {
            if (entries[i].value == value) {
                return entries[i].name;
            }
        }
        return nullptr;
    }   
};