// Copyright (c) 2024 - Dylan Knutson <dymk@dymk.co>
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/FixedCircularBuffer.h"

TEST(FixedCircularBuffer, Empty) {
    FixedCircularBuffer<int> buffer(0);

    ASSERT_TRUE(buffer.is_empty());
    ASSERT_EQ(buffer.position(), 0);
    ASSERT_EQ(buffer.at(0), std::nullopt);
    ASSERT_EQ(buffer.at(1), std::nullopt);
    ASSERT_EQ(buffer.at(2), std::nullopt);
}

TEST(FixedCircularBuffer, OneElement) {
    FixedCircularBuffer<int> buffer(1);

    buffer.push(42);

    ASSERT_FALSE(buffer.is_empty());
    ASSERT_EQ(buffer.position(), 1);
    ASSERT_EQ(buffer.at(0), 42);
    ASSERT_EQ(buffer.at(1), std::nullopt);
    ASSERT_EQ(buffer.at(2), std::nullopt);
}

TEST(FixedCircularBuffer, FrontElementsPopped) {
    FixedCircularBuffer<int> buffer(2);

    buffer.push(1);
    buffer.push(2);
    buffer.push(3);

    ASSERT_FALSE(buffer.is_empty());
    ASSERT_EQ(buffer.position(), 3);
    ASSERT_EQ(buffer.at(0), std::nullopt);
    ASSERT_EQ(buffer.at(1), 2);
    ASSERT_EQ(buffer.at(2), 3);
    ASSERT_EQ(buffer.at(3), std::nullopt);
}
