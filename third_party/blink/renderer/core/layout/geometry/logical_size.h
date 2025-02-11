// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_GEOMETRY_LOGICAL_SIZE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_GEOMETRY_LOGICAL_SIZE_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/layout/geometry/logical_offset.h"
#include "third_party/blink/renderer/core/layout/ng/geometry/ng_box_strut.h"
#include "third_party/blink/renderer/platform/geometry/layout_unit.h"
#include "third_party/blink/renderer/platform/text/writing_mode.h"

namespace blink {

struct LogicalOffset;

// TODO(wangxianzhu): Make it a constexpr when LayoutUnit allows it.
#define kIndefiniteSize LayoutUnit(-1)

// LogicalSize is the size of rect (typically a fragment) in the logical
// coordinate system.
struct CORE_EXPORT LogicalSize {
  LogicalSize() = default;
  LogicalSize(LayoutUnit inline_size, LayoutUnit block_size)
      : inline_size(inline_size), block_size(block_size) {}

  // Use ToPhysicalSize to convert to a physical size.

  LayoutUnit inline_size;
  LayoutUnit block_size;

  bool operator==(const LogicalSize& other) const {
    return std::tie(other.inline_size, other.block_size) ==
           std::tie(inline_size, block_size);
  }
  bool operator!=(const LogicalSize& other) const { return !(*this == other); }

  bool IsEmpty() const {
    return inline_size == LayoutUnit() || block_size == LayoutUnit();
  }

  void Flip() { std::swap(inline_size, block_size); }
};

inline LogicalSize& operator-=(LogicalSize& a, const NGBoxStrut& b) {
  a.inline_size -= b.InlineSum();
  a.block_size -= b.BlockSum();
  return a;
}

inline LogicalOffset operator+(const LogicalOffset& offset,
                               const LogicalSize& size) {
  return {offset.inline_offset + size.inline_size,
          offset.block_offset + size.block_size};
}

inline LogicalOffset& operator+=(LogicalOffset& offset,
                                 const LogicalSize& size) {
  offset = offset + size;
  return offset;
}

CORE_EXPORT std::ostream& operator<<(std::ostream&, const LogicalSize&);

// LogicalDelta resolves the ambiguity of subtractions.
//
// "offset - offset" is ambiguous because both of below are true:
//   offset + offset = offset
//   offset + size = offset
//
// LogicalDelta resolves this ambiguity by allowing implicit conversions both
// to LogicalOffset and to LogicalSize.
struct CORE_EXPORT LogicalDelta : public LogicalSize {
 public:
  using LogicalSize::LogicalSize;
  operator LogicalOffset() const { return {inline_size, block_size}; }
};

inline LogicalDelta operator-(const LogicalOffset& a, const LogicalOffset& b) {
  return {a.inline_offset - b.inline_offset, a.block_offset - b.block_offset};
}

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_GEOMETRY_LOGICAL_SIZE_H_
