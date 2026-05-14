#pragma once

#include <src/Assertion.h>

struct TestBase {
    TestBase() : next(nullptr) {}
    TestBase* next;

    virtual const char* unitTestCase() const = 0;
    virtual const char* unitTestName() const = 0;

#ifdef ESP32
    typedef void (*TestFunction)();
    virtual TestFunction getFunction() = 0;
#else
#endif
    virtual void run() = 0;
};

class TestFactory {
    TestBase* first;
    TestBase* last;

    TestFactory() : first(nullptr), last(nullptr) {}
    TestFactory(const TestFactory& o) = default;

public:
    static TestFactory& instance() {
        static TestFactory instance_;
        return instance_;
    }

    void registerTest(TestBase* test) {
        if (last == nullptr) {
            first = last = test;
        } else {
            last->next = test;
            last       = test;
        }
    }

    void runAll();
};
