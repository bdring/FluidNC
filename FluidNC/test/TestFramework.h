#pragma once

#ifdef ESP32

#    include <Arduino.h>
#    include "unity.h"

#    include <src/Assert.h>
#    include "TestFactory.h"

#    define TEST_CLASS_NAME(testCase, testName) testCase##_##testName##_Test
#    define TEST_INST_NAME(testCase, testName) testCase##_##testName##_Test_Instance

// Defines a single unit test. Basically creates a small test class.

#    define Test(testCase, testName)                                                                                                       \
        struct TEST_CLASS_NAME(testCase, testName) : TestBase {                                                                            \
            TEST_CLASS_NAME(testCase, testName)() { TestFactory::instance().registerTest(this); }                                          \
                                                                                                                                           \
            const char* unitTestCase() const override { return #testCase; }                                                                \
            const char* unitTestName() const override { return #testName; }                                                                \
                                                                                                                                           \
            static void runDetail();                                                                                                       \
            static void runWrap() {                                                                                                        \
                try {                                                                                                                      \
                    runDetail();                                                                                                           \
                } catch (AssertionFailed ex) { TEST_FAIL_MESSAGE(ex.stackTrace.c_str()); } catch (...) {                                   \
                    TEST_FAIL_MESSAGE("Failed for unknown reason.");                                                                       \
                }                                                                                                                          \
            }                                                                                                                              \
            void run() override { runWrap(); }                                                                                             \
                                                                                                                                           \
            TestFunction getFunction() override { return runWrap; }                                                                        \
        };                                                                                                                                 \
                                                                                                                                           \
        TEST_CLASS_NAME(testCase, testName) TEST_INST_NAME(testCase, testName);                                                            \
                                                                                                                                           \
        void TEST_CLASS_NAME(testCase, testName)::runDetail()

#    define NativeTest(testCase, testName) TEST_INST_NAME(testCase, testName)
#    define PlatformTest(testCase, testName) Test(testCase, testName)

inline void PrintSerial(const char* format, ...) {
    va_list arg;
    va_list copy;
    va_start(arg, format);
    va_copy(copy, arg);
    size_t len = vsnprintf(NULL, 0, format, arg);
    auto   tmp = new char[len + 1];
    va_end(copy);
    len = vsnprintf(tmp, len + 1, format, arg);
    Serial.println(tmp);
    va_end(arg);
    delete[] tmp;
}

#    define Debug(fmt, ...) PrintSerial(fmt, __VA_ARGS__);

#    define AssertThrow(statement)                                                                                                         \
        try {                                                                                                                              \
            statement;                                                                                                                     \
            Assert(false, "Expected statement to throw.");                                                                                 \
        } catch (...) {}

#elif defined _WIN32 || defined _WIN64

#    include "../src/Assert.h"

// Use 'Assert(...)' please.

#    define GTEST_DONT_DEFINE_TEST 1
#    define GTEST_DONT_DEFINE_ASSERT_EQ 1
#    define GTEST_DONT_DEFINE_ASSERT_NE 1
#    define GTEST_DONT_DEFINE_ASSERT_LT 1
#    define GTEST_DONT_DEFINE_ASSERT_LE 1
#    define GTEST_DONT_DEFINE_ASSERT_GE 1
#    define GTEST_DONT_DEFINE_ASSERT_GT 1
#    define GTEST_DONT_DEFINE_FAIL 1
#    define GTEST_DONT_DEFINE_SUCCEED 1

#    include "gtest/gtest.h"

#    undef EXPECT_THROW
#    undef EXPECT_NO_THROW
#    undef EXPECT_ANY_THROW
#    undef ASSERT_THROW
#    undef ASSERT_NO_THROW
#    undef ASSERT_ANY_THROW

#    define Test(test_case_name, test_name) GTEST_TEST(test_case_name, test_name)

#    define NativeTest(testCase, testName) Test(testCase, testName)
#    define PlatformTest(testCase, testName) TEST_INST_NAME(testCase, testName)

#    define Debug(fmt, ...)                                                                                                                \
        printf(fmt, __VA_ARGS__);                                                                                                          \
        printf("\r\n");

#    define AssertThrow(statement) GTEST_TEST_ANY_THROW_(statement, GTEST_FATAL_FAILURE_)

#else

#    include <src/Assert.h>
#    include "TestFactory.h"

#    define TEST_CLASS_NAME(testCase, testName) testCase##_##testName##_Test
#    define TEST_INST_NAME(testCase, testName) testCase##_##testName##_Test_Instance

// Defines a single unit test. Basically creates a small test class.

#    define Test(testCase, testName)                                                                                                       \
        struct TEST_CLASS_NAME(testCase, testName) : TestBase {                                                                            \
            TEST_CLASS_NAME(testCase, testName)() { TestFactory::instance().registerTest(this); }                                          \
                                                                                                                                           \
            const char* unitTestCase() const override { return #testCase; }                                                                \
            const char* unitTestName() const override { return #testName; }                                                                \
            void        run() override;                                                                                                    \
        };                                                                                                                                 \
                                                                                                                                           \
        TEST_CLASS_NAME(testCase, testName) TEST_INST_NAME(testCase, testName);                                                            \
                                                                                                                                           \
        void TEST_CLASS_NAME(testCase, testName)::run()

#    define NativeTest(testCase, testName) Test(testCase, testName)
#    define PlatformTest(testCase, testName) TEST_INST_NAME(testCase, testName)

#    define Debug(fmt, ...) printf(fmt, __VA_ARGS__);

#    define AssertThrow(statement)                                                                                                         \
        try {                                                                                                                              \
            statement;                                                                                                                     \
            Assert(false, "Expected statement to throw.");                                                                                 \
        } catch (...) {}

#endif
