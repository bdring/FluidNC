#include "TestFramework.h"

#include "Pins/PinOptionsParser.h"
#include <cstdio>
#include <cstring>

namespace Pins {
    Test(PinOptionParsing, NoArgs) {
        char                   nullDescr[1] = { '\0' };
        Pins::PinOptionsParser parser(nullDescr, nullDescr);

        {
            auto opt    = parser.begin();
            auto endopt = parser.end();
            Assert(opt == endopt, "Expected empty enumerator");
        }

        // Typical use is a for loop. Let's test the two ways to use it:
        for (auto it : parser) {
            Assert(false, "Didn't expect to get here");
        }

        for (auto it = parser.begin(); it != parser.end(); ++it) {
            Assert(false, "Didn't expect to get here");
        }
    }

    Test(PinOptionParsing, SingleArg) {
        const char* input = "first";
        char        tmp[20];
        int         n = snprintf(tmp, 20, "%s", input);

        Pins::PinOptionsParser parser(tmp, tmp + n);

        {
            auto opt    = parser.begin();
            auto endopt = parser.end();
            Assert(opt != endopt, "Expected an argument");
            Assert(opt->is("first"), "Expected 'first'");

            ++opt;
            Assert(opt == endopt, "Expected one argument");
        }

        // Typical use is a for loop. Let's test the two ways to use it:
        int ctr = 0;
        for (auto it : parser) {
            if (ctr == 0) {
                Assert(it.is("first"), "Expected 'first'");
            } else {
                Assert(false, "Didn't expect to get here");
            }
            ++ctr;
        }
    }

    Test(PinOptionParsing, SingleArgWithWS) {
        const char* input = "  first";
        char        tmp[20];
        int         n = snprintf(tmp, 20, "%s", input);

        Pins::PinOptionsParser parser(tmp, tmp + n);

        {
            auto opt    = parser.begin();
            auto endopt = parser.end();
            Assert(opt != endopt, "Expected an argument");
            Assert(opt->is("first"), "Expected 'first'");

            ++opt;
            Assert(opt == endopt, "Expected one argument");
        }

        // Typical use is a for loop. Let's test the two ways to use it:
        int ctr = 0;
        for (auto it : parser) {
            if (ctr == 0) {
                Assert(it.is("first"), "Expected 'first'");
            } else {
                Assert(false, "Didn't expect to get here");
            }
            ++ctr;
        }
    }

    Test(PinOptionParsing, SingleArgWithWS2) {
        const char* input = "  first  ";
        char        tmp[20];
        int         n = snprintf(tmp, 20, "%s", input);

        Pins::PinOptionsParser parser(tmp, tmp + n);

        {
            auto opt    = parser.begin();
            auto endopt = parser.end();
            Assert(opt != endopt, "Expected an argument");
            Assert(opt->is("first"), "Expected 'first'");

            ++opt;
            Assert(opt == endopt, "Expected one argument");
        }

        // Typical use is a for loop. Let's test the two ways to use it:
        int ctr = 0;
        for (auto it : parser) {
            if (ctr == 0) {
                Assert(it.is("first"), "Expected 'first'");
            } else {
                Assert(false, "Didn't expect to get here");
            }
            ++ctr;
        }
    }

    Test(PinOptionParsing, TwoArg1) {
        const char* input = "first;second";
        char        tmp[20];
        int         n = snprintf(tmp, 20, "%s", input);

        Pins::PinOptionsParser parser(tmp, tmp + n);

        {
            auto opt    = parser.begin();
            auto endopt = parser.end();
            Assert(opt != endopt, "Expected an argument");
            Assert(opt->is("first"), "Expected 'first'");

            ++opt;
            Assert(opt != endopt, "Expected second argument");
            Assert(opt->is("second"), "Expected 'second'");

            ++opt;
            Assert(opt == endopt, "Expected one argument");
        }

        // Typical use is a for loop. Let's test the two ways to use it:
        int ctr = 0;
        for (auto it : parser) {
            if (ctr == 0) {
                Assert(it.is("first"), "Expected 'first'");
            } else if (ctr == 1) {
                Assert(it.is("second"), "Expected 'second'");
            } else {
                Assert(false, "Didn't expect to get here");
            }
            ++ctr;
        }
    }

    Test(PinOptionParsing, TwoArg2) {
        const char* input = "first:second";
        char        tmp[20];
        int         n = snprintf(tmp, 20, "%s", input);

        Pins::PinOptionsParser parser(tmp, tmp + n);

        {
            auto opt    = parser.begin();
            auto endopt = parser.end();
            Assert(opt != endopt, "Expected an argument");
            Assert(opt->is("first"), "Expected 'first'");

            ++opt;
            Assert(opt != endopt, "Expected second argument");
            Assert(opt->is("second"), "Expected 'second'");

            ++opt;
            Assert(opt == endopt, "Expected one argument");
        }

        // Typical use is a for loop. Let's test the two ways to use it:
        int ctr = 0;
        for (auto it : parser) {
            if (ctr == 0) {
                Assert(it.is("first"), "Expected 'first'");
            } else if (ctr == 1) {
                Assert(it.is("second"), "Expected 'second'");
            } else {
                Assert(false, "Didn't expect to get here");
            }
            ++ctr;
        }
    }

    Test(PinOptionParsing, TwoArgWithValues) {
        const char* input = "first=12;second=13";
        char        tmp[20];
        int         n = snprintf(tmp, 20, "%s", input);

        Pins::PinOptionsParser parser(tmp, tmp + n);

        {
            auto opt    = parser.begin();
            auto endopt = parser.end();
            Assert(opt != endopt, "Expected an argument");
            Assert(opt->is("first"), "Expected 'first'");
            Assert(strcmp("12", opt->value()) == 0);
            Assert(12 == opt->iValue());
            Assert(12 == opt->dValue());

            ++opt;
            Assert(opt != endopt, "Expected second argument");
            Assert(opt->is("second"), "Expected 'second'");
            Assert(strcmp("13", opt->value()) == 0);
            Assert(13 == opt->iValue());
            Assert(13 == opt->dValue());

            ++opt;
            Assert(opt == endopt, "Expected one argument");
        }

        // Typical use is a for loop. Let's test the two ways to use it:
        int ctr = 0;
        for (auto it : parser) {
            if (ctr == 0) {
                Assert(it.is("first"), "Expected 'first'");
            } else if (ctr == 1) {
                Assert(it.is("second"), "Expected 'second'");
            } else {
                Assert(false, "Didn't expect to get here");
            }
            ++ctr;
        }
    }
}
