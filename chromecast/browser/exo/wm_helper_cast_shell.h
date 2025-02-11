// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMECAST_BROWSER_EXO_WM_HELPER_CAST_SHELL_H_
#define CHROMECAST_BROWSER_EXO_WM_HELPER_CAST_SHELL_H_

#include <cstdint>
#include <map>
#include <vector>

#include "base/macros.h"
#include "base/observer_list.h"
#include "components/exo/vsync_timing_manager.h"
#include "components/exo/wm_helper.h"
#include "ui/aura/client/drag_drop_delegate.h"
#include "ui/base/cursor/cursor.h"
#include "ui/display/display_observer.h"

namespace aura {
class env;
class Window;
namespace client {
class CursorClient;
class FocusChangeObserver;
}  // namespace client
}  // namespace aura

namespace chromecast {
class CastWindowManagerAura;
}

namespace wm {
class ActivationChangeObserver;
}

namespace display {
class ManagedDisplayInfo;
}

namespace ui {
class EventHandler;
class DropTargetEvent;
}  // namespace ui

namespace wm {
class ActivationChangeObserver;
}

namespace chromecast {
class CastScreen;
}

namespace exo {

// A CastShell-specific helper class for accessing WindowManager related
// features.
class WMHelperCastShell : public WMHelper, public VSyncTimingManager::Delegate {
 public:
  WMHelperCastShell(aura::Env* env,
                    chromecast::CastWindowManagerAura* cast_window_manager_aura,
                    chromecast::CastScreen* cast_screen);
  ~WMHelperCastShell() override;

  // Overridden from WMHelper
  aura::Env* env() override;
  void AddActivationObserver(wm::ActivationChangeObserver* observer) override;
  void RemoveActivationObserver(
      wm::ActivationChangeObserver* observer) override;
  void AddFocusObserver(aura::client::FocusChangeObserver* observer) override;
  void RemoveFocusObserver(
      aura::client::FocusChangeObserver* observer) override;

  void AddDragDropObserver(DragDropObserver* observer) override;
  void RemoveDragDropObserver(DragDropObserver* observer) override;
  void SetDragDropDelegate(aura::Window*) override;
  void ResetDragDropDelegate(aura::Window*) override;
  VSyncTimingManager& GetVSyncTimingManager() override;

  const display::ManagedDisplayInfo& GetDisplayInfo(
      int64_t display_id) const override;
  const std::vector<uint8_t>& GetDisplayIdentificationData(
      int64_t display_id) const override;

  aura::Window* GetPrimaryDisplayContainer(int container_id) override;
  aura::Window* GetActiveWindow() const override;
  aura::Window* GetFocusedWindow() const override;
  aura::Window* GetRootWindowForNewWindows() const override;
  aura::client::CursorClient* GetCursorClient() override;
  void AddPreTargetHandler(ui::EventHandler* handler) override;
  void PrependPreTargetHandler(ui::EventHandler* handler) override;
  void RemovePreTargetHandler(ui::EventHandler* handler) override;
  void AddPostTargetHandler(ui::EventHandler* handler) override;
  void RemovePostTargetHandler(ui::EventHandler* handler) override;
  bool IsTabletModeWindowManagerEnabled() const override;
  double GetDefaultDeviceScaleFactor() const override;

  LifetimeManager* GetLifetimeManager() override;
  aura::client::CaptureClient* GetCaptureClient() override;

  // Overridden from aura::client::DragDropDelegate:
  void OnDragEntered(const ui::DropTargetEvent& event) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  int OnPerformDrop(const ui::DropTargetEvent& event) override;

  // Overridden from VSyncTimingManager::Delegate:
  void AddVSyncParameterObserver(
      viz::mojom::VSyncParameterObserverPtr observer) override;

 private:
  class CastDisplayObserver : public display::DisplayObserver {
   public:
    CastDisplayObserver();
    ~CastDisplayObserver() override;

    // Overridden from display::DisplayObserver
    void OnWillProcessDisplayChanges() override;
    void OnDidProcessDisplayChanges() override;
    void OnDisplayAdded(const display::Display& new_display) override;
    void OnDisplayRemoved(const display::Display& old_display) override;
    void OnDisplayMetricsChanged(const display::Display& display,
                                 uint32_t changed_metrics) override;

    const display::ManagedDisplayInfo& GetDisplayInfo(int64_t display_id) const;

   private:
    std::map<int64_t, display::ManagedDisplayInfo> display_info_;
  };

  chromecast::CastWindowManagerAura* cast_window_manager_aura_;
  aura::Env* const env_;
  chromecast::CastScreen* cast_screen_;
  CastDisplayObserver display_observer_;
  LifetimeManager lifetime_manager_;
  VSyncTimingManager vsync_timing_manager_;

  DISALLOW_COPY_AND_ASSIGN(WMHelperCastShell);
};

}  // namespace exo

#endif  // CHROMECAST_BROWSER_EXO_WM_HELPER_CAST_SHELL_H_
