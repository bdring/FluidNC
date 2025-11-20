#include "TestFramework.h"

#include <src/Configuration/Tokenizer.h>
#include <src/Configuration/Parser.h>

namespace Configuration {
    Test(YamlParser, BasicProperties) {
        const char* config = "a: aap\n"
                             "b: banaan\n"
                             "\n"
                             "c: chocolade\n";

        Parser p(config, config + strlen(config));
        p.Tokenize();
        {
            Assert(p.key().equals("a"), "Expected 'a'");
            Assert(p.stringValue().equals("aap"), "Expected 'aap'");
        }

        p.Tokenize();
        {
            Assert(p.key().equals("b"), "Expected 'b'");
            Assert(p.stringValue().equals("banaan"), "Expected 'banaan'");
        }

        p.Tokenize();
        {
            Assert(p.key().equals("c"), "Expected 'c'");
            Assert(p.stringValue().equals("chocolade"), "Expected 'chocolade'");
        }
        p.Tokenize();
        Assert(p.Eof(), "EOF failed");
    }

    Test(YamlParser, SimpleSection) {
        const char* config = "a: aap\n"
                             "s:\n"
                             "  b: banaan\n"
                             "\n"
                             "c: chocolade\n";

        Parser p(config, config + strlen(config));
        p.Tokenize();
        {
            Assert(p.key().equals("a"), "Expected 'a'");
            Assert(p.stringValue().equals("aap"), "Expected 'aap'");
        }

        p.Tokenize();
        {
            Assert(p.key().equals("s"), "Expected 's'");
            {
                p.Tokenize();
                Assert(p.key().equals("b"), "Expected 'b'");
                Assert(p.stringValue().equals("banaan"), "Expected 'banaan'");
            }
        }

        p.Tokenize();
        {
            Assert(p.key().equals("c"), "Expected 'c'");
            Assert(p.stringValue().equals("chocolade"), "Expected 'chocolade'");
        }
        p.Tokenize();
        Assert(p.Eof(), "EOF failed");
    }

    Test(YamlParser, TwoSequentialSections) {
        const char* config = "a: aap\n"
                             "s:\n"
                             "  b: banaan\n"
                             "t:\n"
                             "  c: chocolade\n"
                             "\n"
                             "w: wipwap\n";

        Parser p(config, config + strlen(config));
        p.Tokenize();
        {
            Assert(p.key().equals("a"), "Expected 'a'");
            Assert(p.stringValue().equals("aap"), "Expected 'aap'");
        }

        p.Tokenize();
        {
            Assert(p.key().equals("s"), "Expected 's'");
            p.Tokenize();
            {
                Assert(p.key().equals("b"), "Expected 'b'");
                Assert(p.stringValue().equals("banaan"), "Expected 'banaan'");
            }
        }

        p.Tokenize();
        {
            Assert(p.key().equals("t"), "Expected 't'");
            p.Tokenize();
            {
                Assert(p.key().equals("c"), "Expected 'c'");
                Assert(p.stringValue().equals("chocolade"), "Expected 'chocolade'");
            }
        }

        p.Tokenize();
        {
            Assert(p.key().equals("w"), "Expected 'w'");
            Assert(p.stringValue().equals("wipwap"), "Expected 'wipwap'");
        }
        p.Tokenize();
        Assert(p.Eof(), "EOF failed");
    }

    Test(YamlParser, TwoSequentialSectionsInASection) {
        const char* config = "a: aap\n"
                             "r:\n"
                             "  s:\n"
                             "    b: banaan\n"
                             "    d: dinges\n"
                             "  t:\n"
                             "    c: chocolade\n"
                             "    e: eventjes\n"
                             "\n"
                             "w: wipwap\n";

        Parser p(config, config + strlen(config));
        p.Tokenize();
        {
            Assert(p.key().equals("a"), "Expected 'a'");
            Assert(p.stringValue().equals("aap"), "Expected 'aap'");
        }

        p.Tokenize();
        {
            Assert(p.key().equals("r"), "Expected 'r'");
            p.Tokenize();

            {
                Assert(p.key().equals("s"), "Expected 's'");
                p.Tokenize();
                {
                    Assert(p.key().equals("b"), "Expected 'b'");
                    Assert(p.stringValue().equals("banaan"), "Expected 'banaan'");
                }
                p.Tokenize();
                {
                    Assert(p.key().equals("d"), "Expected 'd'");
                    Assert(p.stringValue().equals("dinges"), "Expected 'dinges'");
                }
            }

            p.Tokenize();
            {
                Assert(p.key().equals("t"), "Expected 't'");
                p.Tokenize();
                {
                    Assert(p.key().equals("c"), "Expected 'c'");
                    Assert(p.stringValue().equals("chocolade"), "Expected 'chocolade'");
                }
                p.Tokenize();
                {
                    Assert(p.key().equals("e"), "Expected 'e'");
                    Assert(p.stringValue().equals("eventjes"), "Expected 'eventjes'");
                }
            }
        }

        p.Tokenize();
        {
            Assert(p.key().equals("w"), "Expected 'w'");
            Assert(p.stringValue().equals("wipwap"), "Expected 'wipwap'");
        }
        p.Tokenize();
        Assert(p.Eof(), "EOF failed");
    }
}
