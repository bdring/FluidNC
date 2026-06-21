#include <gtest/gtest.h>
#include <system_error>

#include "../src/FluidError.hpp"

namespace {

TEST(FluidError, NoneErrorCodeIsZero) {
    auto ec = std::error_code(FluidError::None);
    EXPECT_EQ(ec.value(), 0);
}

TEST(FluidError, SDNotConfiguredErrorCodeIsOne) {
    auto ec = std::error_code(FluidError::SDNotConfigured);
    EXPECT_EQ(ec.value(), 1);
}

TEST(FluidError, ErrorCodeCategoryName) {
    auto ec = std::error_code(FluidError::None);
    EXPECT_STREQ(ec.category().name(), "FluidError");
}

TEST(FluidError, ErrorMessageForNone) {
    auto ec = std::error_code(FluidError::None);
    EXPECT_STREQ(ec.message().c_str(), "None");
}

TEST(FluidError, ErrorMessageForSDNotConfigured) {
    auto ec = std::error_code(FluidError::SDNotConfigured);
    EXPECT_STREQ(ec.message().c_str(), "SDCard not configured");
}

}  // namespace
