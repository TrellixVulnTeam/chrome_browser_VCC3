// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_PUBLIC_CPP_PAGINATION_PAGINATION_CONTROLLER_H_
#define ASH_PUBLIC_CPP_PAGINATION_PAGINATION_CONTROLLER_H_

#include "ash/public/cpp/ash_public_export.h"
#include "base/macros.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"

namespace gfx {
class Vector2d;
class Rect;
}  // namespace gfx

namespace ash {

class PaginationModel;
// Receives user scroll events from various sources (mouse wheel, touchpad,
// touch gestures) and manipulates a PaginationModel as necessary.
class ASH_PUBLIC_EXPORT PaginationController {
 public:
  enum ScrollAxis { SCROLL_AXIS_HORIZONTAL, SCROLL_AXIS_VERTICAL };

  using RecordMetrics = base::RepeatingCallback<void(ui::EventType)>;

  // Creates a PaginationController. Does not take ownership of |model|. The
  // |model| is required to outlive this PaginationController. |scroll_axis|
  // specifies the axis in which the pages will scroll.
  PaginationController(PaginationModel* model,
                       ScrollAxis scroll_axis,
                       const RecordMetrics& record_metrics);
  ~PaginationController();

  ScrollAxis scroll_axis() const { return scroll_axis_; }

  // Handles a mouse wheel or touchpad scroll event in the area represented by
  // the PaginationModel. |offset| is the number of units scrolled in each axis.
  // Returns true if the event was captured and there was some room to scroll.
  bool OnScroll(const gfx::Vector2d& offset, ui::EventType type);

  // Handles a touch gesture event in the area represented by the
  // PaginationModel. Returns true if the event was captured.
  bool OnGestureEvent(const ui::GestureEvent& event, const gfx::Rect& bounds);

 private:
  PaginationModel* pagination_model_;  // Not owned.
  ScrollAxis scroll_axis_;

  const RecordMetrics record_metrics_;

  DISALLOW_COPY_AND_ASSIGN(PaginationController);
};

}  // namespace ash

#endif  // ASH_PUBLIC_CPP_PAGINATION_PAGINATION_CONTROLLER_H_
