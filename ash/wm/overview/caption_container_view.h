// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_OVERVIEW_CAPTION_CONTAINER_VIEW_H_
#define ASH_WM_OVERVIEW_CAPTION_CONTAINER_VIEW_H_

#include "ash/ash_export.h"
#include "base/macros.h"
#include "ui/views/controls/button/button.h"

namespace aura {
class Window;
}  // namespace aura

namespace views {
class ImageButton;
class ImageView;
class Label;
class View;
}  // namespace views

namespace ash {

class RoundedRectView;

// CaptionContainerView covers the overview window and listens for events. It
// also draws a header for overview mode which contains a icon, title and close
// button.
// TODO(sammiequon): Rename this to something which describes it better.
class ASH_EXPORT CaptionContainerView : public views::Button {
 public:
  // The visibility of the header. It may be fully visible or invisible, or
  // everything but the close button is visible.
  enum class HeaderVisibility {
    kInvisible,
    kCloseButtonInvisibleOnly,
    kVisible,
  };

  class EventDelegate {
   public:
    // TODO: Maybe consolidate into just mouse and gesture events.
    virtual void HandlePressEvent(const gfx::PointF& location_in_screen) = 0;
    virtual void HandleDragEvent(const gfx::PointF& location_in_screen) = 0;
    virtual void HandleReleaseEvent(const gfx::PointF& location_in_screen) = 0;
    virtual void HandleFlingStartEvent(const gfx::PointF& location_in_screen,
                                       float velocity_x,
                                       float velocity_y) = 0;
    virtual void HandleLongPressEvent(
        const gfx::PointF& location_in_screen) = 0;
    virtual void HandleTapEvent() = 0;
    virtual void HandleGestureEndEvent() = 0;
    virtual bool ShouldIgnoreGestureEvents() = 0;

   protected:
    virtual ~EventDelegate() {}
  };

  CaptionContainerView(EventDelegate* event_delegate,
                       aura::Window* window,
                       views::ImageButton* close_button);
  ~CaptionContainerView() override;

  // Fades the app icon and title out if |visibility| is kInvisible, in
  // otherwise. If |close_button_| is not null, also fades the close button in
  // if |visibility| is kVisible, out otherwise. Sets
  // |current_header_visibility_| to |visibility|.
  void SetHeaderVisibility(HeaderVisibility visibility);

  // Hides the close button instantaneously, and then fades it in slowly and
  // with a long delay. Sets |current_header_visibility_| to kVisible. Assumes
  // that |close_button_| is not null, and that |current_header_visibility_| is
  // not kInvisible.
  void HideCloseInstantlyAndThenShowItSlowly();

  // Sets the visiblity of |backdrop_view_|. Creates it if it is null.
  void SetBackdropVisibility(bool visible);

  void ResetEventDelegate();

  // Set the title of the view, and also updates the accessiblity name.
  void SetTitle(const base::string16& title);

  views::View* header_view() { return header_view_; }
  views::Label* title_label() { return title_label_; }
  RoundedRectView* backdrop_view() { return backdrop_view_; }

 protected:
  // views::View:
  void Layout() override;
  const char* GetClassName() const override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnGestureEvent(ui::GestureEvent* event) override;
  bool CanAcceptEvent(const ui::Event& event) override;

 private:
  // The delegate which all the events get forwarded to.
  EventDelegate* event_delegate_;

  // View which contains the icon, title and an optional close button.
  views::View* header_view_ = nullptr;
  views::Label* title_label_ = nullptr;
  views::ImageView* image_view_ = nullptr;
  views::ImageButton* close_button_;

  // A view that covers the area except the header. It is null when the window
  // associated is not pillar or letter boxed.
  RoundedRectView* backdrop_view_ = nullptr;

  HeaderVisibility current_header_visibility_ = HeaderVisibility::kVisible;

  DISALLOW_COPY_AND_ASSIGN(CaptionContainerView);
};

}  // namespace ash

#endif  // ASH_WM_OVERVIEW_CAPTION_CONTAINER_VIEW_H_
