// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "device/vr/windows_mixed_reality/wrappers/wmr_holographic_frame.h"

#include <windows.graphics.holographic.h>
#include <wrl.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "base/logging.h"
#include "base/trace_event/common/trace_event_common.h"
#include "base/trace_event/trace_event.h"
#include "device/vr/windows_mixed_reality/wrappers/wmr_rendering.h"
#include "device/vr/windows_mixed_reality/wrappers/wmr_timestamp.h"

using PresentResult =
    ABI::Windows::Graphics::Holographic::HolographicFramePresentResult;
using ABI::Windows::Foundation::Collections::IVectorView;
using ABI::Windows::Graphics::Holographic::HolographicCameraPose;
using ABI::Windows::Graphics::Holographic::IHolographicCameraPose;
using ABI::Windows::Graphics::Holographic::
    IHolographicCameraRenderingParameters;
using ABI::Windows::Graphics::Holographic::IHolographicFrame;
using ABI::Windows::Graphics::Holographic::IHolographicFramePrediction;
using ABI::Windows::Perception::IPerceptionTimestamp;
using Microsoft::WRL::ComPtr;

namespace device {

// WMRHolographicFramePrediction
WMRHolographicFramePrediction::WMRHolographicFramePrediction(
    ComPtr<IHolographicFramePrediction> prediction)
    : prediction_(prediction) {
  DCHECK(prediction_);
}

WMRHolographicFramePrediction::~WMRHolographicFramePrediction() = default;

std::unique_ptr<WMRTimestamp> WMRHolographicFramePrediction::Timestamp() {
  ComPtr<IPerceptionTimestamp> timestamp;
  HRESULT hr = prediction_->get_Timestamp(&timestamp);
  DCHECK(SUCCEEDED(hr));
  return std::make_unique<WMRTimestamp>(timestamp);
}

std::vector<std::unique_ptr<WMRCameraPose>>
WMRHolographicFramePrediction::CameraPoses() {
  std::vector<std::unique_ptr<WMRCameraPose>> ret_val;
  ComPtr<IVectorView<HolographicCameraPose*>> poses;
  HRESULT hr = prediction_->get_CameraPoses(&poses);
  if (FAILED(hr))
    return ret_val;

  uint32_t num;
  hr = poses->get_Size(&num);
  DCHECK(SUCCEEDED(hr));

  for (uint32_t i = 0; i < num; i++) {
    ComPtr<IHolographicCameraPose> pose;
    poses->GetAt(i, &pose);
    ret_val.push_back(std::make_unique<WMRCameraPose>(pose));
  }

  return ret_val;
}

// WMRHolographicFrame
WMRHolographicFrame::WMRHolographicFrame(
    ComPtr<IHolographicFrame> holographic_frame)
    : holographic_frame_(holographic_frame) {
  DCHECK(holographic_frame_);
}

WMRHolographicFrame::~WMRHolographicFrame() = default;

std::unique_ptr<WMRHolographicFramePrediction>
WMRHolographicFrame::CurrentPrediction() {
  ComPtr<IHolographicFramePrediction> prediction;
  HRESULT hr = holographic_frame_->get_CurrentPrediction(&prediction);
  DCHECK(SUCCEEDED(hr));
  return std::make_unique<WMRHolographicFramePrediction>(prediction);
}

std::unique_ptr<WMRRenderingParameters>
WMRHolographicFrame::TryGetRenderingParameters(const WMRCameraPose* pose) {
  ComPtr<IHolographicCameraRenderingParameters> rendering_params;
  HRESULT hr = holographic_frame_->GetRenderingParameters(pose->GetRawPtr(),
                                                          &rendering_params);
  if (FAILED(hr))
    return nullptr;
  return std::make_unique<WMRRenderingParameters>(rendering_params);
}

bool WMRHolographicFrame::TryPresentUsingCurrentPrediction() {
  PresentResult result = PresentResult::HolographicFramePresentResult_Success;
  HRESULT hr = holographic_frame_->PresentUsingCurrentPrediction(&result);

  TRACE_EVENT_INSTANT2("xr", "SubmitWMR", TRACE_EVENT_SCOPE_THREAD, "hr", hr,
                       "result", result);

  if (FAILED(hr))
    return false;

  return true;
}
}  // namespace device
