#include "../TestFramework.h"

#include <string>

#include <src/Configuration/Tokenizer.h>
#include <src/Configuration/Parser.h>
#include <src/Configuration/ParserHandler.h>
#include <src/Configuration/Configurable.h>

namespace Configuration {
    class TestBasic : public Configurable {
    public:
        String a;
        String b;
        String c;

        void validate() {}
        void group(HandlerBase& handler) {
            handler.item("a", a);
            handler.item("b", b);
            handler.item("c", c);
        }
    };

    class TestBasic2 : public Configurable {
    public:
        String aap;
        int    banaan;

        void validate() {}
        void group(HandlerBase& handler) {
            handler.item("aap", aap);
            handler.item("banaan", banaan);
        }
    };

    enum stepper_id_t {
        ST_TIMED = 0,
        ST_RMT,
        ST_I2S_STREAM,
        ST_I2S_STATIC,
    };

    EnumItem stepTypes[] = {
        { ST_TIMED, "Timed" }, { ST_RMT, "RMT" }, { ST_I2S_STATIC, "I2S_static" }, { ST_I2S_STREAM, "I2S_stream" }, EnumItem(ST_RMT)
    };

    class TestBasicEnum : public Configurable {
    public:
        int aap;
        int value;
        int banaan;

        void validate() {}
        void group(HandlerBase& handler) override {
            handler.item("aap", aap);
            handler.item("type", value, stepTypes);
            handler.item("banaan", banaan);
        }
    };

    class TestHierarchical : public Configurable {
    public:
        TestBasic*  n1  = nullptr;
        TestBasic2* n2  = nullptr;
        int         foo = 0;

        void validate() {}
        void group(HandlerBase& handler) override {
            handler.section("n1", n1);
            handler.section("n2", n2);
            handler.item("foo", foo);
        }
    };

    struct Helper {
        template <typename T>
        static inline void Parse(const char* config, T& test) {
            Parser        p(config, config + strlen(config));
            ParserHandler handler(p);

            handler.enterSection("machine", &test);
            // test.group(handler);
            // for (; !p.Eof(); handler.moveNext()) {
            //     test.group(handler);
            // }
        }
    };

    Test(YamlTreeBuilder, BasicProperties) {
        const char* config = "a: aap\n"
                             "b: banaan\n"
                             "\n"
                             "c: chocolade\n";

        TestBasic test;
        Helper::Parse(config, test);

        Assert(test.a == "aap");
        Assert(test.b == "banaan");
        Assert(test.c == "chocolade");
    }

    Test(YamlTreeBuilder, BasicPropertiesInvert) {
        const char* config = "c: chocolade\n"
                             "b: banaan\n"
                             "a: aap\n";

        TestBasic test;
        Helper::Parse(config, test);

        Assert(test.a == "aap");
        Assert(test.b == "banaan");
        Assert(test.c == "chocolade");
    }

    Test(YamlTreeBuilder, BasicProperties2) {
        const char* config = "aap: aap\n"
                             "banaan: 2\n";

        TestBasic2 test;
        Helper::Parse(config, test);

        Assert(test.aap == "aap");
        Assert(test.banaan == 2);
    }

    Test(YamlTreeBuilder, BasicPropertiesInvert2) {
        const char* config = "banaan: 2\n"
                             "aap: aap\n";

        TestBasic2 test;
        Helper::Parse(config, test);

        Assert(test.aap == "aap");
        Assert(test.banaan == 2);
    }

    Test(YamlTreeBuilder, Hierarchical1) {
        const char* config = "startup_line1:\n"
                             "  a: aap\n"
                             "  b: banaan\n"
                             "  \n"
                             "  c: chocolade\n"
                             "n2:\n"
                             "  banaan: 2\n"
                             "  aap: aap\n"
                             "foo: 2\n";

        TestHierarchical test;
        Helper::Parse(config, test);

        {
            Assert(test.n1 != nullptr);
            Assert(test.n1->a == "aap");
            Assert(test.n1->b == "banaan");
            Assert(test.n1->c == "chocolade");
        }

        {
            Assert(test.n2 != nullptr);
            Assert(test.n2->banaan == 2);
            Assert(test.n2->aap == "aap");
        }
        Assert(test.foo == 2);
    }

    Test(YamlTreeBuilder, Hierarchical2) {
        const char* config = "n2:\n"
                             "  banaan: 2\n"
                             "  aap: aap\n"
                             "startup_line1:\n"
                             "  a: aap\n"
                             "  b: banaan\n"
                             "  \n"
                             "  c: chocolade\n"
                             "foo: 2\n";

        TestHierarchical test;
        Helper::Parse(config, test);

        {
            Assert(test.n1 != nullptr);
            Assert(test.n1->a == "aap");
            Assert(test.n1->b == "banaan");
            Assert(test.n1->c == "chocolade");
        }

        {
            Assert(test.n2 != nullptr);
            Assert(test.n2->banaan == 2);
            Assert(test.n2->aap == "aap");
        }
        Assert(test.foo == 2);
    }

    Test(YamlTreeBuilder, Hierarchical3) {
        const char* config = "foo: 2\n"
                             "n2:\n"
                             "  banaan: 2\n"
                             "  aap: aap\n"
                             "startup_line1:\n"
                             "  a: aap\n"
                             "  b: banaan\n"
                             "  \n"
                             "  c: chocolade\n";

        TestHierarchical test;
        Helper::Parse(config, test);

        {
            Assert(test.n1 != nullptr);
            Assert(test.n1->a == "aap");
            Assert(test.n1->b == "banaan");
            Assert(test.n1->c == "chocolade");
        }

        {
            Assert(test.n2 != nullptr);
            Assert(test.n2->banaan == 2);
            Assert(test.n2->aap == "aap");
        }
        Assert(test.foo == 2);
    }

    //  ST_TIMED, "Timed" }, { ST_RMT, "RMT" }, { ST_I2S_STATIC, "I2S_static" }, { ST_I2S_STREAM, "I2S_stream" }, EnumItem()
    Test(YamlTreeBuilder, Enum1) {
        {
            const char*   config = "aap: 1\ntype: Timed\nbanaan: 2\n";
            TestBasicEnum test;
            Helper::Parse(config, test);
            Assert(test.value == int(ST_TIMED));
        }

        {
            const char*   config = "aap: 1\ntype: RMT\nbanaan: 2\n";
            TestBasicEnum test;
            Helper::Parse(config, test);
            Assert(test.value == int(ST_RMT));
        }
        {
            const char*   config = "aap: 1\ntype: I2S_static\nbanaan: 2\n";
            TestBasicEnum test;
            Helper::Parse(config, test);
            Assert(test.value == int(ST_I2S_STATIC));
        }
        {
            const char*   config = "aap: 1\ntype: I2S_stream\nbanaan: 2\n";
            TestBasicEnum test;
            Helper::Parse(config, test);
            Assert(test.value == int(ST_I2S_STREAM));
        }
    }
}
