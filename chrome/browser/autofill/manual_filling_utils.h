// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOFILL_MANUAL_FILLING_UTILS_H_
#define CHROME_BROWSER_AUTOFILL_MANUAL_FILLING_UTILS_H_

#include "components/autofill/core/browser/accessory_sheet_data.h"

namespace autofill {

// Creates an AccessorySheetData defining the data to be shown in the filling
// UI.
AccessorySheetData CreateAccessorySheetData(base::string16 title,
                                            std::vector<UserInfo> user_info);

}  // namespace autofill

#endif  // CHROME_BROWSER_AUTOFILL_MANUAL_FILLING_UTILS_H_
