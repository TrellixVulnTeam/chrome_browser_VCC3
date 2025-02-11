// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NGColumnLayoutAlgorithm_h
#define NGColumnLayoutAlgorithm_h

#include "third_party/blink/renderer/core/layout/ng/ng_box_fragment_builder.h"
#include "third_party/blink/renderer/core/layout/ng/ng_layout_algorithm.h"

namespace blink {

class NGBlockNode;
class NGBlockBreakToken;
class NGBreakToken;
class NGConstraintSpace;
struct LogicalSize;

class CORE_EXPORT NGColumnLayoutAlgorithm
    : public NGLayoutAlgorithm<NGBlockNode,
                               NGBoxFragmentBuilder,
                               NGBlockBreakToken> {
 public:
  NGColumnLayoutAlgorithm(NGBlockNode node,
                          const NGFragmentGeometry&,
                          const NGConstraintSpace& space,
                          const NGBreakToken* break_token = nullptr);

  scoped_refptr<const NGLayoutResult> Layout() override;

  base::Optional<MinMaxSize> ComputeMinMaxSize(
      const MinMaxSizeInput&) const override;

 private:
  LogicalSize CalculateColumnSize(const LogicalSize& content_box_size);
  LayoutUnit CalculateBalancedColumnBlockSize(const LogicalSize& column_size,
                                              int column_count);

  // Stretch the column length, if allowed. We do this during column balancing,
  // when we discover that the current length isn't large enough to fit all
  // content.
  LayoutUnit StretchColumnBlockSize(
      LayoutUnit minimal_space_shortage,
      LayoutUnit current_column_size,
      LayoutUnit container_content_box_block_size) const;

  LayoutUnit ConstrainColumnBlockSize(LayoutUnit size) const;

  NGConstraintSpace CreateConstraintSpaceForColumns(
      const LogicalSize& column_size,
      bool separate_leading_margins) const;
  NGConstraintSpace CreateConstraintSpaceForBalancing(
      const LogicalSize& column_size) const;
  NGConstraintSpace CreateConstraintSpaceForMinMax() const;

  const NGBoxStrut border_padding_;
  const NGBoxStrut border_scrollbar_padding_;
};

}  // namespace blink

#endif  // NGColumnLayoutAlgorithm_h
