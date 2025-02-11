// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/omnibox/remove_suggestion_bubble.h"

#include <memory>
#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "chrome/grit/generated_resources.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/strings/grit/components_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace {

class RemoveSuggestionBubbleDialogDelegateView
    : public views::BubbleDialogDelegateView {
 public:
  RemoveSuggestionBubbleDialogDelegateView(views::View* anchor_view,
                                           const AutocompleteMatch& match,
                                           base::OnceClosure remove_closure)
      : views::BubbleDialogDelegateView(anchor_view,
                                        views::BubbleBorder::TOP_LEFT),
        match_(match),
        remove_closure_(std::move(remove_closure)) {
    auto* layout_manager = SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::kVertical));
    layout_manager->set_cross_axis_alignment(
        views::BoxLayout::CROSS_AXIS_ALIGNMENT_START);
    // TODO(tommycli): Replace this with the real spacing from UX.
    layout_manager->set_between_child_spacing(16);
    // TODO(tommycli): Replace this with the real translated string from UX.
    views::Label* why_this_suggestion_label =
        new views::Label(match.GetWhyThisSuggestionText());
    why_this_suggestion_label->SetMultiLine(true);
    why_this_suggestion_label->SetHorizontalAlignment(
        gfx::HorizontalAlignment::ALIGN_LEFT);
    AddChildView(why_this_suggestion_label);

    AddChildView(new views::Label(base::ASCIIToUTF16(
        match.SupportsDeletion() ? "Remove suggestion from history?"
                                 : "This match cannot be removed.")));
  }

  // views::DialogDelegateView:
  int GetDialogButtons() const override {
    int buttons = ui::DIALOG_BUTTON_CANCEL;
    if (match_.SupportsDeletion())
      buttons |= ui::DIALOG_BUTTON_OK;
    return buttons;
  }
  base::string16 GetDialogButtonLabel(ui::DialogButton button) const override {
    // TODO(tommycli): Replace this with the real translated string from UX.
    if (button == ui::DIALOG_BUTTON_OK)
      return base::ASCIIToUTF16("Remove");

    return l10n_util::GetStringUTF16(match_.SupportsDeletion() ? IDS_CANCEL
                                                               : IDS_CLOSE);
  }
  bool Accept() override {
    DCHECK(match_.SupportsDeletion());
    std::move(remove_closure_).Run();
    return true;
  }

  // views::View:
  gfx::Size CalculatePreferredSize() const override {
    // TODO(tommycli): Replace with the real width from UX.
    return gfx::Size(500, GetHeightForWidth(500));
  }

  // views::WidgetDelegate:
  ui::ModalType GetModalType() const override { return ui::MODAL_TYPE_WINDOW; }
  base::string16 GetWindowTitle() const override { return match_.contents; }

 private:
  AutocompleteMatch match_;
  base::OnceClosure remove_closure_;
};

}  // namespace

void ShowRemoveSuggestion(views::View* anchor_view,
                          const AutocompleteMatch& match,
                          base::OnceClosure remove_closure) {
  views::BubbleDialogDelegateView::CreateBubble(
      new RemoveSuggestionBubbleDialogDelegateView(anchor_view, match,
                                                   std::move(remove_closure)))
      ->Show();
}
