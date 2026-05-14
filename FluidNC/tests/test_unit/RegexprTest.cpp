#include <gtest/gtest.h>
#include <string_view>

// Forward declare the regex function
bool regexMatch(std::string_view regexp, std::string_view text, bool case_sensitive = true);

// Test basic literal matching
TEST(Regexpr, LiteralExactMatch) {
    EXPECT_TRUE(regexMatch("hello", "hello", true));
}

TEST(Regexpr, LiteralNoMatch) {
    EXPECT_FALSE(regexMatch("hello", "world", true));
}

TEST(Regexpr, LiteralPartialMatchInMiddle) {
    EXPECT_TRUE(regexMatch("ell", "hello", true));
}

TEST(Regexpr, LiteralPartialMatchAtEnd) {
    EXPECT_TRUE(regexMatch("lo", "hello", true));
}

TEST(Regexpr, EmptyRegexp) {
    EXPECT_TRUE(regexMatch("", "anything", true));
    EXPECT_TRUE(regexMatch("", "", true));
}

TEST(Regexpr, EmptyText) {
    EXPECT_FALSE(regexMatch("pattern", "", true));
    EXPECT_TRUE(regexMatch("", "", true));
}

// Test caret anchor (beginning of string)
TEST(Regexpr, CaretAtBeginning) {
    EXPECT_TRUE(regexMatch("^hello", "hello world", true));
}

TEST(Regexpr, CaretAtBeginningNoMatch) {
    EXPECT_FALSE(regexMatch("^world", "hello world", true));
}

TEST(Regexpr, CaretMustMatchStart) {
    EXPECT_FALSE(regexMatch("^ello", "hello", true));
}

TEST(Regexpr, CaretEmptyString) {
    EXPECT_TRUE(regexMatch("^", "", true));
}

// Test dollar anchor (end of string)
TEST(Regexpr, DollarAtEnd) {
    EXPECT_TRUE(regexMatch("world$", "hello world", true));
}

TEST(Regexpr, DollarAtEndNoMatch) {
    EXPECT_FALSE(regexMatch("hello$", "hello world", true));
}

TEST(Regexpr, DollarOnlyMatch) {
    EXPECT_TRUE(regexMatch("ello$", "hello", true));
}

TEST(Regexpr, DollarEmptyString) {
    EXPECT_TRUE(regexMatch("$", "", true));
}

TEST(Regexpr, CaretAndDollarEmptyMatch) {
    EXPECT_TRUE(regexMatch("^$", "", true));
}

// Test star wildcard (matches zero or more of any character)
TEST(Regexpr, StarMatchZero) {
    EXPECT_TRUE(regexMatch("hel*lo", "hello", true));
}

TEST(Regexpr, StarMatchOne) {
    EXPECT_TRUE(regexMatch("hel*o", "helo", true));
}

TEST(Regexpr, StarMatchMultiple) {
    EXPECT_TRUE(regexMatch("hel*o", "hello", true));
}

TEST(Regexpr, StarMatchMany) {
    EXPECT_TRUE(regexMatch("h*world", "helloworld", true));
}

TEST(Regexpr, StarAtBeginning) {
    EXPECT_TRUE(regexMatch("*world", "helloworld", true));
    EXPECT_TRUE(regexMatch("*world", "world", true));
}

TEST(Regexpr, StarAtEnd) {
    EXPECT_TRUE(regexMatch("hello*", "hello", true));
    EXPECT_TRUE(regexMatch("hello*", "helloworld", true));
}

TEST(Regexpr, MultipleStar) {
    EXPECT_TRUE(regexMatch("h*e*l*o", "helloworld", true));
}

// Test combined anchors and wildcards
TEST(Regexpr, CaretWithStar) {
    EXPECT_TRUE(regexMatch("^hel*", "helloworld", true));
    EXPECT_TRUE(regexMatch("^hel*", "heloworld", true));
}

TEST(Regexpr, StarWithDollar) {
    EXPECT_TRUE(regexMatch("hel*$", "hello", true));
    EXPECT_TRUE(regexMatch("hel*$", "helo", true));
}

TEST(Regexpr, CaretStarDollar) {
    EXPECT_TRUE(regexMatch("^hel*$", "helo", true));
    EXPECT_TRUE(regexMatch("^hel*$", "helox", true));  // helo matches at start
}

// Test case sensitivity
TEST(Regexpr, CaseSensitiveExactMatch) {
    EXPECT_TRUE(regexMatch("Hello", "Hello", true));
}

TEST(Regexpr, CaseSensitiveDifferentCase) {
    EXPECT_FALSE(regexMatch("Hello", "hello", true));
}

TEST(Regexpr, CaseInsensitiveMatch) {
    EXPECT_TRUE(regexMatch("Hello", "hello", false));
}

TEST(Regexpr, CaseInsensitivePartialMatch) {
    EXPECT_TRUE(regexMatch("HEL", "hello", false));   // Case insensitive partial match
    EXPECT_TRUE(regexMatch("h*o", "HELLO", false));   // Case insensitive with wildcard
}

TEST(Regexpr, CaseInsensitiveWithStar) {
    EXPECT_TRUE(regexMatch("HEL*O", "hello", false));
}

TEST(Regexpr, CaseInsensitiveWithAnchors) {
    EXPECT_TRUE(regexMatch("^HELLO$", "hello", false));
}

// Test single characters
TEST(Regexpr, SingleCharMatch) {
    EXPECT_TRUE(regexMatch("a", "a", true));
}

TEST(Regexpr, SingleCharNoMatch) {
    EXPECT_FALSE(regexMatch("a", "b", true));
}

// Test numbers
TEST(Regexpr, NumericPattern) {
    EXPECT_TRUE(regexMatch("123", "123", true));
}

TEST(Regexpr, NumericPartial) {
    EXPECT_TRUE(regexMatch("123", "0123456", true));
}

// Test special characters
TEST(Regexpr, SpecialCharPeriod) {
    EXPECT_TRUE(regexMatch(".", ".", true));
}

TEST(Regexpr, SpecialCharDash) {
    EXPECT_TRUE(regexMatch("-", "hello-world", true));
}

TEST(Regexpr, SpecialCharUnderscore) {
    EXPECT_TRUE(regexMatch("_", "hello_world", true));
}

// Test real-world setting name patterns
TEST(Regexpr, SettingNamePattern1) {
    EXPECT_TRUE(regexMatch("stepper*enable", "stepper0_enable", false));
}

TEST(Regexpr, SettingNamePattern2) {
    EXPECT_TRUE(regexMatch("stepper*enable", "stepper1_enable", false));
}

TEST(Regexpr, SettingNamePattern3) {
    EXPECT_TRUE(regexMatch("stepper*enable", "stepper_enable", false));
}

TEST(Regexpr, SettingNameNoMatch) {
    EXPECT_FALSE(regexMatch("stepper*enable", "stepper0_disable", false));
}

// Test edge cases
TEST(Regexpr, VeryLongText) {
    std::string long_text(1000, 'a');
    EXPECT_TRUE(regexMatch("*", long_text, true));
}

TEST(Regexpr, RepeatedCharacters) {
    EXPECT_TRUE(regexMatch("aaa", "aaaaaa", true));
}

TEST(Regexpr, MultipleWildcardsComplex) {
    EXPECT_TRUE(regexMatch("s*e*t*g", "setting", true));
}

TEST(Regexpr, CaretDollarWithContent) {
    EXPECT_TRUE(regexMatch("^test$", "test", true));
    EXPECT_FALSE(regexMatch("^test$", "testing", true));
}

// Test anchor combinations
TEST(Regexpr, CaretWithWildcardAndDollar) {
    EXPECT_TRUE(regexMatch("^hel*$", "hello", true));
    EXPECT_TRUE(regexMatch("^hel*$", "helo", true));
}

// Test boundary conditions
TEST(Regexpr, OnlyCaretAnchor) {
    EXPECT_TRUE(regexMatch("^", "hello", true));
}

TEST(Regexpr, OnlyDollarAnchor) {
    EXPECT_TRUE(regexMatch("$", "hello", true));
}

TEST(Regexpr, OnlyStarWildcard) {
    EXPECT_TRUE(regexMatch("*", "hello", true));
}

// Test that wildcards work in all positions
TEST(Regexpr, WildcardAtStart) {
    EXPECT_TRUE(regexMatch("*hello", "xyzahello", true));
}

TEST(Regexpr, WildcardInMiddle) {
    EXPECT_TRUE(regexMatch("hel*lo", "hellooo", true));
}

TEST(Regexpr, WildcardAtEnd) {
    EXPECT_TRUE(regexMatch("hello*", "helloxyz", true));
}

// Test sequential matching
TEST(Regexpr, SequentialPatterns) {
    EXPECT_TRUE(regexMatch("abc", "xyzabc123", true));
}

TEST(Regexpr, SequentialWithWildcard) {
    EXPECT_TRUE(regexMatch("a*c", "axxxbxxxc", true));
}

// Test case sensitivity combinations
TEST(Regexpr, CaseSensitiveMultipleMatches) {
    EXPECT_FALSE(regexMatch("^Hel*$", "hello", true));
    EXPECT_TRUE(regexMatch("^hel*$", "hello", true));
}

TEST(Regexpr, CaseInsensitiveMultipleMatches) {
    EXPECT_TRUE(regexMatch("^HEL*$", "hello", false));
    EXPECT_TRUE(regexMatch("^hel*$", "HELLO", false));
}

// Test greedy matching with wildcards
TEST(Regexpr, GreedyWildcard1) {
    EXPECT_TRUE(regexMatch("h*o", "hello", true));
}

TEST(Regexpr, GreedyWildcard2) {
    EXPECT_TRUE(regexMatch("h*e*l*o", "hello", true));
}

// Test realistic setting name patterns
TEST(Regexpr, MotorSettingPattern) {
    EXPECT_TRUE(regexMatch("motor*speed", "motor0speed", false));
    EXPECT_TRUE(regexMatch("motor*speed", "motor1speed", false));
}

TEST(Regexpr, PinNamePattern) {
    EXPECT_TRUE(regexMatch("gpio*enable", "gpio2enable", false));
}

// Additional comprehensive tests
TEST(Regexpr, ComplexPattern) {
    EXPECT_TRUE(regexMatch("^test*123$", "test123", true));
}

TEST(Regexpr, NoConsecutiveStars) {
    // While ** is technically valid, it should work correctly
    EXPECT_TRUE(regexMatch("a**b", "axxxb", true));
}

TEST(Regexpr, PatternVsNoPattern) {
    EXPECT_TRUE(regexMatch("test", "test", true));
    EXPECT_TRUE(regexMatch("*test*", "pretest-post", true));
}
