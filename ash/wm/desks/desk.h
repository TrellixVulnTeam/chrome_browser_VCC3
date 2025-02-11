// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_DESKS_DESK_H_
#define ASH_WM_DESKS_DESK_H_

#include <memory>

#include "ash/ash_export.h"
#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/macros.h"
#include "ui/aura/window_observer.h"

namespace ash {

class DeskContainerObserver;

// Represents a virtual desk, tracking the windows that belong to this desk.
// In a multi display scenario, desks span all displays (i.e. if desk 1 is
// active, it is active on all displays at the same time). Each display is
// associated with a |container_id_|. This is the ID of all the containers on
// all root windows that are associated with this desk. So the mapping is: one
// container per display (root window) per each desk.
// Those containers are parent windows of the windows that belong to the
// associated desk. When the desk is active, those containers are shown, when
// the desk is in active, those containers are hidden.
class ASH_EXPORT Desk : public aura::WindowObserver {
 public:
  explicit Desk(int associated_container_id);
  ~Desk() override;

  int container_id() const { return container_id_; }

  const base::flat_set<aura::Window*>& windows() const { return windows_; }

  bool is_active() const { return is_active_; }

  void OnRootWindowAdded(aura::Window* root);
  void OnRootWindowClosing(aura::Window* root);

  void AddWindowToDesk(aura::Window* window);

  // Activates this desk. All windows on this desk (if any) will become visible
  // (by means of showing this desk's associated containers on all root
  // windows), and the most recently used one of them will be activated.
  void Activate();

  // Deactivates this desk. All windows on this desk (if any) will become hidden
  // (by means of hiding this desk's associated containers on all root windows),
  // and the currently active window on this desk will be deactivated.
  void Deactivate();

  // Moves the windows on this desk to |target_desk|.
  void MoveWindowsToDesk(Desk* target_desk);

  aura::Window* GetDeskContainerForRoot(aura::Window* root) const;

  // aura::WindowObserver:
  void OnWindowDestroyed(aura::Window* window) override;

 private:
  // The associated container ID with this desk.
  const int container_id_;

  // Windows tracked on this desk. Clients of the DesksController can use this
  // list when they're notified of desk change events.
  base::flat_set<aura::Window*> windows_;

  // Maps all root windows to observer objects observing the containers
  // associated with this desk on those root windows.
  base::flat_map<aura::Window*, std::unique_ptr<DeskContainerObserver>>
      roots_to_containers_observers_;

  // TODO(afakhry): Consider removing this.
  bool is_active_ = false;

  DISALLOW_COPY_AND_ASSIGN(Desk);
};

}  // namespace ash

#endif  // ASH_WM_DESKS_DESK_H_
