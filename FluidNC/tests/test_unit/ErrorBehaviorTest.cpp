#include <gtest/gtest.h>

#include "../src/Error.h"

TEST(ErrorBehavior, ErrorNamesProvidesMessagesForKnownErrors) {
    // Exercises a code path that consumes the `Error` enum for serialization.
    auto it = ErrorNames.find(Error::InvalidStatement);
    ASSERT_NE(it, ErrorNames.end());
    ASSERT_NE(it->second, nullptr);
    EXPECT_NE(std::string_view(it->second).find("Invalid"), std::string_view::npos);
}

TEST(ErrorBehavior, ErrorNamesLookupFailsForUnknownError) {
    // Observable behavior: unknown entries are not present.
    auto it = ErrorNames.find(static_cast<Error>(255));
    EXPECT_EQ(it, ErrorNames.end());
}
