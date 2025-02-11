// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/tab_strip_layout_helper.h"

#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/tab_style.h"
#include "chrome/browser/ui/views/tabs/tab.h"
#include "chrome/browser/ui/views/tabs/tab_strip_layout.h"
#include "chrome/browser/ui/views/tabs/tab_style_views.h"
#include "ui/base/material_design/material_design_controller.h"
#include "ui/views/view_model.h"

namespace {

const TabSizeInfo& GetTabSizeInfo() {
  static TabSizeInfo tab_size_info, touch_tab_size_info;
  TabSizeInfo* info = ui::MaterialDesignController::touch_ui()
                          ? &touch_tab_size_info
                          : &tab_size_info;
  if (info->standard_size.IsEmpty()) {
    info->pinned_tab_width = TabStyle::GetPinnedWidth();
    info->min_active_width = TabStyleViews::GetMinimumActiveWidth();
    info->min_inactive_width = TabStyleViews::GetMinimumInactiveWidth();
    info->standard_size =
        gfx::Size(TabStyle::GetStandardWidth(), GetLayoutConstant(TAB_HEIGHT));
    info->tab_overlap = TabStyle::GetTabOverlap();
  }
  return *info;
}

}  // namespace

TabStripLayoutHelper::TabStripLayoutHelper()
    : active_tab_width_(TabStyle::GetStandardWidth()),
      inactive_tab_width_(TabStyle::GetStandardWidth()),
      first_non_pinned_tab_index_(0),
      first_non_pinned_tab_x_(0) {}

TabStripLayoutHelper::~TabStripLayoutHelper() = default;

int TabStripLayoutHelper::GetPinnedTabCount(
    const views::ViewModelT<Tab>* tabs) const {
  int pinned_count = 0;
  while (pinned_count < tabs->view_size() &&
         tabs->view_at(pinned_count)->data().pinned) {
    pinned_count++;
  }
  return pinned_count;
}

void TabStripLayoutHelper::UpdateIdealBounds(views::ViewModelT<Tab>* tabs,
                                             int available_width,
                                             int active_tab_index) {
  const std::vector<gfx::Rect> tab_bounds =
      CalculateTabBounds(GetTabSizeInfo(), GetPinnedTabCount(tabs),
                         tabs->view_size(), active_tab_index, available_width,
                         &active_tab_width_, &inactive_tab_width_);
  DCHECK_EQ(static_cast<size_t>(tabs->view_size()), tab_bounds.size());

  for (size_t i = 0; i < tab_bounds.size(); ++i)
    tabs->set_ideal_bounds(i, tab_bounds[i]);
}

void TabStripLayoutHelper::UpdateIdealBoundsForPinnedTabs(
    views::ViewModelT<Tab>* tabs) {
  const int pinned_tab_count = GetPinnedTabCount(tabs);

  first_non_pinned_tab_index_ = pinned_tab_count;
  first_non_pinned_tab_x_ = 0;

  if (pinned_tab_count > 0) {
    std::vector<gfx::Rect> tab_bounds(tabs->view_size());
    first_non_pinned_tab_x_ = CalculateBoundsForPinnedTabs(
        GetTabSizeInfo(), pinned_tab_count, tabs->view_size(), &tab_bounds);
    for (int i = 0; i < pinned_tab_count; ++i)
      tabs->set_ideal_bounds(i, tab_bounds[i]);
  }
}
