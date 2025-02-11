// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_BASE_GET_SESSION_NAME_H_
#define COMPONENTS_SYNC_BASE_GET_SESSION_NAME_H_

#include <string>

namespace syncer {

// TODO(crbug.com/922971): Move this elsewhere in components/sync/device_info.
std::string GetSessionNameBlocking();

}  // namespace syncer

#endif  // COMPONENTS_SYNC_BASE_GET_SESSION_NAME_H_
