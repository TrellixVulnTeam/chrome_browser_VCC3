// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/geometry/physical_size.h"

#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

String PhysicalSize::ToString() const {
  return String::Format("%dx%d", width.ToInt(), height.ToInt());
}

std::ostream& operator<<(std::ostream& os, const PhysicalSize& value) {
  return os << value.ToString();
}

}  // namespace blink
