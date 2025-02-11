// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/geometry/logical_rect.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

namespace {

struct LogicalRectUniteTestData {
  const char* test_case;
  LogicalRect a;
  LogicalRect b;
  LogicalRect expected;
} logical_rect_unite_test_data[] = {
    {"empty", {}, {}, {}},
    {"a empty",
     {},
     {{LayoutUnit(1), LayoutUnit(2)}, {LayoutUnit(3), LayoutUnit(4)}},
     {{LayoutUnit(1), LayoutUnit(2)}, {LayoutUnit(3), LayoutUnit(4)}}},
    {"b empty",
     {{LayoutUnit(1), LayoutUnit(2)}, {LayoutUnit(3), LayoutUnit(4)}},
     {},
     {{LayoutUnit(1), LayoutUnit(2)}, {LayoutUnit(3), LayoutUnit(4)}}},
    {"a larger",
     {{LayoutUnit(100), LayoutUnit(50)}, {LayoutUnit(300), LayoutUnit(200)}},
     {{LayoutUnit(200), LayoutUnit(50)}, {LayoutUnit(200), LayoutUnit(200)}},
     {{LayoutUnit(100), LayoutUnit(50)}, {LayoutUnit(300), LayoutUnit(200)}}},
    {"b larger",
     {{LayoutUnit(200), LayoutUnit(50)}, {LayoutUnit(200), LayoutUnit(200)}},
     {{LayoutUnit(100), LayoutUnit(50)}, {LayoutUnit(300), LayoutUnit(200)}},
     {{LayoutUnit(100), LayoutUnit(50)}, {LayoutUnit(300), LayoutUnit(200)}}},
};

std::ostream& operator<<(std::ostream& os,
                         const LogicalRectUniteTestData& data) {
  return os << "Unite " << data.test_case;
}

class LogicalRectUniteTest
    : public testing::Test,
      public testing::WithParamInterface<LogicalRectUniteTestData> {};

INSTANTIATE_TEST_SUITE_P(GeometryUnitsTest,
                         LogicalRectUniteTest,
                         testing::ValuesIn(logical_rect_unite_test_data));

TEST_P(LogicalRectUniteTest, Data) {
  const auto& data = GetParam();
  LogicalRect actual = data.a;
  actual.Unite(data.b);
  EXPECT_EQ(data.expected, actual);
}

}  // namespace

}  // namespace blink
