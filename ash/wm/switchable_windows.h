// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_SWITCHABLE_WINDOWS_H_
#define ASH_WM_SWITCHABLE_WINDOWS_H_

#include <vector>

#include "ash/ash_export.h"

namespace aura {
class Window;
}

namespace ash {
namespace wm {

ASH_EXPORT std::vector<aura::Window*> GetSwitchableContainersForRoot(
    aura::Window* root);

// Returns true if |window| is a container for windows which can be switched to.
ASH_EXPORT bool IsSwitchableContainer(const aura::Window* window);

}  // namespace wm
}  // namespace ash

#endif  // ASH_WM_SWITCHABLE_WINDOWS_H_
