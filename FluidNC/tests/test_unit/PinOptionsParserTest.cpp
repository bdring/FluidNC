// Copyright (c) 2023 - Dylan Knutson <dymk@dymk.co>
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "gtest/gtest.h"
#include "Pins/PinOptionsParser.h"

using PinOptionsParser = Pins::PinOptionsParser;
static void test_for_loop(const PinOptionsParser& parser);
static void test_for_loop_only_first(const PinOptionsParser& parser);

TEST(PinOptionsParser, WithEmptyString) {
    PinOptionsParser parser("");

    {
        auto opt    = parser.begin();
        auto endopt = parser.end();
        ASSERT_EQ(opt, endopt) << "Expected empty enumerator";
    }

    // Typical use is a for loop. Let's test the two ways to use it:
    for (auto it : parser) {
        FAIL() << "Didn't expect to get here";
    }

    for (auto it = parser.begin(); it != parser.end(); ++it) {
        FAIL() << "Didn't expect to get here";
    }
}

TEST(PinOptionsParser, SingleArg) {
    PinOptionsParser parser("first");

    {
        auto opt    = parser.begin();
        auto endopt = parser.end();
        ASSERT_NE(opt, endopt) << "Expected an argument";
        ASSERT_TRUE(opt.is("first")) << "Expected 'first'";
        ++opt;
        ASSERT_EQ(opt, endopt) << "Expected one argument";
    }

    test_for_loop_only_first(parser);
}

TEST(PinOptionsParser, SingleArgWithWS) {
    PinOptionsParser parser("   first");

    {
        auto opt    = parser.begin();
        auto endopt = parser.end();
        ASSERT_NE(opt, endopt) << "Expected an argument";
        ASSERT_TRUE(opt.is("first")) << "Expected 'first'";
        ++opt;
        ASSERT_EQ(opt, endopt) << "Expected one argument";
    }

    test_for_loop_only_first(parser);
}

TEST(PinOptionsParser, SingleArgWithWS2) {
    PinOptionsParser parser("  first  ");
    {
        auto opt    = parser.begin();
        auto endopt = parser.end();
        ASSERT_NE(opt, endopt) << "Expected an argument";
        ASSERT_TRUE(opt.is("first")) << "Expected 'first'";

        ++opt;
        ASSERT_EQ(opt, endopt) << "Expected one argument";
    }

    test_for_loop_only_first(parser);
}

TEST(PinOptionsParser, TwoArg1) {
    PinOptionsParser parser("first;second");
    {
        auto opt    = parser.begin();
        auto endopt = parser.end();
        ASSERT_NE(opt, endopt) << "Expected an argument";
        ASSERT_TRUE(opt.is("first")) << "Expected 'first'";

        ++opt;
        ASSERT_NE(opt, endopt) << "Expected second argument";
        ASSERT_TRUE(opt.is("second")) << "Expected 'second'";

        ++opt;
        ASSERT_EQ(opt, endopt) << "Expected one argument";
    }

    test_for_loop(parser);
}

TEST(PinOptionsParser, TwoArg2) {
    PinOptionsParser parser("first:second");
    {
        auto opt    = parser.begin();
        auto endopt = parser.end();
        ASSERT_NE(opt, endopt) << "Expected an argument";
        ASSERT_TRUE(opt.is("first")) << "Expected 'first'";

        ++opt;
        ASSERT_NE(opt, endopt) << "Expected second argument";
        ASSERT_TRUE(opt.is("second")) << "Expected 'second'";

        ++opt;
        ASSERT_EQ(opt, endopt) << "Expected one argument";
    }

    test_for_loop(parser);
}

TEST(PinOptionsParser, TwoArgWithValues) {
    PinOptionsParser parser("first=12;second=13");

    {
        auto opt    = parser.begin();
        auto endopt = parser.end();
        ASSERT_NE(opt, endopt) << "Expected an argument";
        ASSERT_TRUE(opt.is("first")) << "Expected 'first'";
        ASSERT_TRUE(opt.value() == "12");
        ASSERT_EQ(12, opt.iValue());

        ++opt;
        ASSERT_NE(opt, endopt) << "Expected second argument";
        ASSERT_TRUE(opt.is("second")) << "Expected 'second'";
        ASSERT_TRUE(opt.value() == "13");
        ASSERT_EQ(13, opt.iValue());

        ++opt;
        ASSERT_EQ(opt, endopt) << "Expected one argument";
    }

    test_for_loop(parser);
}

static void test_for_loop(const PinOptionsParser& parser) {
    // Typical use is a for loop. Let's test the two ways to use it:
    int ctr = 0;
    for (auto it : parser) {
        if (ctr == 0) {
            ASSERT_TRUE(it.is("first")) << "Expected 'first'";
        } else if (ctr == 1) {
            ASSERT_TRUE(it.is("second")) << "Expected 'second'";
        } else {
            FAIL() << "Didn't expect to get here";
        }
        ++ctr;
    }
}

static void test_for_loop_only_first(const PinOptionsParser& parser) {
    // Typical use is a for loop. Let's test the two ways to use it:
    int ctr = 0;
    for (auto it : parser) {
        if (ctr == 0) {
            ASSERT_TRUE(it.is("first")) << "Expected 'first'";
        } else {
            FAIL() << "Didn't expect to get here";
        }
        ++ctr;
    }
}
