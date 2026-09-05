// Copyright (c) 2026 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Drives the real Configuration::Tokenizer class end to end (not a copy of
// its logic) -- covers the '#'-comment/quoting behavior verified by hand
// while making the InLineConfigComments changes: standard-YAML-consistent
// comment detection (whitespace-preceded or start-of-line, quote-aware),
// quoted keys, and the pre-existing quoted-value/whole-line-comment/section
// behavior those changes were careful not to disturb.
//
// Tokenizer holds std::string_view members pointing into whatever it was
// constructed from -- it never owns the backing storage. Every test below
// keeps its YAML text in a named local (`yaml`) that outlives the
// Tokenizer and every view taken from it; a temporary (e.g.
// Tokenizer(std::string("..."))) would leave those views dangling the
// instant the constructor call's full expression ends.

#include "gtest/gtest.h"
#include "Configuration/Tokenizer.h"

#include <string>

using Configuration::Tokenizer;

TEST(Tokenizer, PlainKeyValue) {
    std::string yaml = "key: value";
    Tokenizer   t(yaml);
    t.Tokenize();
    EXPECT_EQ(t.key(), "key");
    EXPECT_EQ(t._token._value, "value");
}

TEST(Tokenizer, TrailingCommentAfterUnquotedValue) {
    // The original bug this whole effort started from: a same-line comment
    // after an unquoted value used to become part of the literal value.
    std::string yaml = "idle_ms: 250 # stay enabled forever";
    Tokenizer   t(yaml);
    t.Tokenize();
    EXPECT_EQ(t.key(), "idle_ms");
    EXPECT_EQ(t._token._value, "250");
}

TEST(Tokenizer, TightHashInUnquotedValueStaysLiteral) {
    // No preceding whitespace -- matches standard YAML, e.g. a macro's
    // G-code parameter reference like '#100' stays part of the value.
    std::string yaml = "macro0: G1 X#100";
    Tokenizer   t(yaml);
    t.Tokenize();
    EXPECT_EQ(t.key(), "macro0");
    EXPECT_EQ(t._token._value, "G1 X#100");
}

TEST(Tokenizer, HashInsideQuotesIsAlwaysLiteral) {
    // Whitespace-preceded '#' would normally start a comment, but not
    // inside a quoted value -- quoting is the escape hatch.
    std::string yaml = "macro0: \"G1 X #100\"";
    Tokenizer   t(yaml);
    t.Tokenize();
    EXPECT_EQ(t.key(), "macro0");
    EXPECT_EQ(t._token._value, "G1 X #100");
}

TEST(Tokenizer, TrailingCommentAfterQuotedValue) {
    std::string yaml = "macro0: \"G1 X #100\" # real trailing comment";
    Tokenizer   t(yaml);
    t.Tokenize();
    EXPECT_EQ(t.key(), "macro0");
    EXPECT_EQ(t._token._value, "G1 X #100");
}

TEST(Tokenizer, BareCommentWhereValueWouldBeIsEmptyValue) {
    std::string yaml = "key: #comment";
    Tokenizer   t(yaml);
    t.Tokenize();
    EXPECT_EQ(t.key(), "key");
    EXPECT_EQ(t._token._value, "");
}

TEST(Tokenizer, WholeLineCommentIsSkippedEntirely) {
    // nextLine()'s do-while loop should skip the comment-only first line
    // within a single Tokenize() call and land directly on the real key.
    std::string yaml = "# just a comment\nkey: value";
    Tokenizer   t(yaml);
    t.Tokenize();
    EXPECT_EQ(t.key(), "key");
    EXPECT_EQ(t._token._value, "value");
}

TEST(Tokenizer, IndentedCommentOnlyLineIsAlsoSkipped) {
    std::string yaml = "outer:\n  # nested comment\n  inner: value";
    Tokenizer   t(yaml);
    t.Tokenize();  // "outer:" (a section header)
    EXPECT_EQ(t.key(), "outer");
    t.Tokenize();  // should skip the comment line and land on "inner"
    EXPECT_EQ(t.key(), "inner");
    EXPECT_EQ(t._token._value, "value");
}

TEST(Tokenizer, CommentBeforeColonBreaksTheMapping) {
    // A whitespace-preceded '#' before the ':' strips the comment -- and
    // the colon along with it -- from the whole line, same as standard
    // YAML; FluidNC has no bare-scalar-document concept, so what's left
    // ("some", no colon) is a hard parse error rather than a silently
    // accepted degenerate document.
    std::string yaml = "some #key: value";
    Tokenizer   t(yaml);
    EXPECT_THROW(t.Tokenize(), std::runtime_error);
}

TEST(Tokenizer, HashEmbeddedInUnquotedKeyWithNoSpaceStaysLiteral) {
    // Not a comment (no preceding whitespace) -- becomes part of the raw
    // key text. Whether that key is ever recognized by anything is a
    // higher-level (ParserHandler) concern, out of scope for the
    // tokenizer itself.
    std::string yaml = "some#key: value";
    Tokenizer   t(yaml);
    t.Tokenize();
    EXPECT_EQ(t.key(), "some#key");
    EXPECT_EQ(t._token._value, "value");
}

TEST(Tokenizer, QuotedKeyBasic) {
    std::string yaml = "\"my key\": value";
    Tokenizer   t(yaml);
    t.Tokenize();
    EXPECT_EQ(t.key(), "my key");
    EXPECT_EQ(t._token._value, "value");
}

TEST(Tokenizer, QuotedKeyWithTrailingComment) {
    std::string yaml = "\"my key\": value # comment";
    Tokenizer   t(yaml);
    t.Tokenize();
    EXPECT_EQ(t.key(), "my key");
    EXPECT_EQ(t._token._value, "value");
}

TEST(Tokenizer, QuotedKeyContainingHash) {
    std::string yaml = "\"key#name\": value";
    Tokenizer   t(yaml);
    t.Tokenize();
    EXPECT_EQ(t.key(), "key#name");
    EXPECT_EQ(t._token._value, "value");
}

TEST(Tokenizer, QuotedKeyContainingColon) {
    std::string yaml = "\"key: with colon\": value";
    Tokenizer   t(yaml);
    t.Tokenize();
    EXPECT_EQ(t.key(), "key: with colon");
    EXPECT_EQ(t._token._value, "value");
}

TEST(Tokenizer, SingleQuotedKeyAndValue) {
    std::string yaml = "'my key': 'my value'";
    Tokenizer   t(yaml);
    t.Tokenize();
    EXPECT_EQ(t.key(), "my key");
    EXPECT_EQ(t._token._value, "my value");
}

TEST(Tokenizer, SequentialLinesParseIndependently) {
    std::string yaml = "first: 1\nsecond: 2 # comment\nthird: \"3#not_a_comment\"";
    Tokenizer   t(yaml);
    t.Tokenize();
    EXPECT_EQ(t.key(), "first");
    EXPECT_EQ(t._token._value, "1");
    t.Tokenize();
    EXPECT_EQ(t.key(), "second");
    EXPECT_EQ(t._token._value, "2");
    t.Tokenize();
    EXPECT_EQ(t.key(), "third");
    EXPECT_EQ(t._token._value, "3#not_a_comment");
}
