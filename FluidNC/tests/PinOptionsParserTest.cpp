// Copyright (c) 2023 - Dylan Knutson <dymk@dymk.co>
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/Pins/PinOptionsParser.h"

using PinOptionsParser = Pins::PinOptionsParser;
static void test_for_loop(PinOptionsParser& parser);
static void test_for_loop_only_first(PinOptionsParser& parser);

TEST(PinOptionsParser, WithEmptyString) {
    char             nullDescr[1] = { '\0' };
    PinOptionsParser parser(nullDescr, nullDescr);

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
    const char* input = "first";
    char        tmp[20];
    int         n = snprintf(tmp, 20, "%s", input);

    PinOptionsParser parser(tmp, tmp + n);

    {
        auto opt    = parser.begin();
        auto endopt = parser.end();
        ASSERT_NE(opt, endopt) << "Expected an argument";
        ASSERT_TRUE(opt->is("first")) << "Expected 'first'";
        ++opt;
        ASSERT_EQ(opt, endopt) << "Expected one argument";
    }

    test_for_loop_only_first(parser);
}

TEST(PinOptionsParser, SingleArgWithWS) {
    const char* input = "  first";
    char        tmp[20];
    int         n = snprintf(tmp, 20, "%s", input);

    PinOptionsParser parser(tmp, tmp + n);

    {
        auto opt    = parser.begin();
        auto endopt = parser.end();
        ASSERT_NE(opt, endopt) << "Expected an argument";
        ASSERT_TRUE(opt->is("first")) << "Expected 'first'";
        ++opt;
        ASSERT_EQ(opt, endopt) << "Expected one argument";
    }

    test_for_loop_only_first(parser);
}

TEST(PinOptionsParser, SingleArgWithWS2) {
    const char* input = "  first  ";
    char        tmp[20];
    int         n = snprintf(tmp, 20, "%s", input);

    PinOptionsParser parser(tmp, tmp + n);

    {
        auto opt    = parser.begin();
        auto endopt = parser.end();
        ASSERT_NE(opt, endopt) << "Expected an argument";
        ASSERT_TRUE(opt->is("first")) << "Expected 'first'";

        ++opt;
        ASSERT_EQ(opt, endopt) << "Expected one argument";
    }

    test_for_loop_only_first(parser);
}

TEST(PinOptionsParser, TwoArg1) {
    const char* input = "first;second";
    char        tmp[20];
    int         n = snprintf(tmp, 20, "%s", input);

    PinOptionsParser parser(tmp, tmp + n);

    {
        auto opt    = parser.begin();
        auto endopt = parser.end();
        ASSERT_NE(opt, endopt) << "Expected an argument";
        ASSERT_TRUE(opt->is("first")) << "Expected 'first'";

        ++opt;
        ASSERT_NE(opt, endopt) << "Expected second argument";
        ASSERT_TRUE(opt->is("second")) << "Expected 'second'";

        ++opt;
        ASSERT_EQ(opt, endopt) << "Expected one argument";
    }

    test_for_loop(parser);
}

TEST(PinOptionsParser, TwoArg2) {
    const char* input = "first:second";
    char        tmp[20];
    int         n = snprintf(tmp, 20, "%s", input);

    PinOptionsParser parser(tmp, tmp + n);

    {
        auto opt    = parser.begin();
        auto endopt = parser.end();
        ASSERT_NE(opt, endopt) << "Expected an argument";
        ASSERT_TRUE(opt->is("first")) << "Expected 'first'";

        ++opt;
        ASSERT_NE(opt, endopt) << "Expected second argument";
        ASSERT_TRUE(opt->is("second")) << "Expected 'second'";

        ++opt;
        ASSERT_EQ(opt, endopt) << "Expected one argument";
    }

    test_for_loop(parser);
}

TEST(PinOptionsParser, TwoArgWithValues) {
    const char* input = "first=12;second=13";
    char        tmp[20];
    int         n = snprintf(tmp, 20, "%s", input);

    PinOptionsParser parser(tmp, tmp + n);

    {
        auto opt    = parser.begin();
        auto endopt = parser.end();
        ASSERT_NE(opt, endopt) << "Expected an argument";
        ASSERT_TRUE(opt->is("first")) << "Expected 'first'";
        ASSERT_EQ(strcmp("12", opt->value()), 0);
        ASSERT_EQ(12, opt->iValue());
        ASSERT_EQ(12, opt->dValue());

        ++opt;
        ASSERT_NE(opt, endopt) << "Expected second argument";
        ASSERT_TRUE(opt->is("second")) << "Expected 'second'";
        ASSERT_EQ(strcmp("13", opt->value()), 0);
        ASSERT_EQ(13, opt->iValue());
        ASSERT_EQ(13, opt->dValue());

        ++opt;
        ASSERT_EQ(opt, endopt) << "Expected one argument";
    }

    test_for_loop(parser);
}

static void test_for_loop(PinOptionsParser& parser) {
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

static void test_for_loop_only_first(PinOptionsParser& parser) {
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
