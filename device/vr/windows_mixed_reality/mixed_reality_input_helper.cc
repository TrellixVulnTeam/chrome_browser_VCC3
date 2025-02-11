// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/vr/windows_mixed_reality/mixed_reality_input_helper.h"

#include <windows.perception.spatial.h>
#include <windows.ui.input.spatial.h>

#include <wrl.h>
#include <wrl/event.h>

#include <unordered_map>
#include <vector>

#include "device/gamepad/public/cpp/gamepads.h"
#include "device/vr/public/mojom/isolated_xr_service.mojom.h"
#include "device/vr/windows_mixed_reality/wrappers/wmr_input_location.h"
#include "device/vr/windows_mixed_reality/wrappers/wmr_input_manager.h"
#include "device/vr/windows_mixed_reality/wrappers/wmr_input_source.h"
#include "device/vr/windows_mixed_reality/wrappers/wmr_input_source_state.h"
#include "device/vr/windows_mixed_reality/wrappers/wmr_origins.h"
#include "device/vr/windows_mixed_reality/wrappers/wmr_pointer_pose.h"
#include "device/vr/windows_mixed_reality/wrappers/wmr_pointer_source_pose.h"
#include "device/vr/windows_mixed_reality/wrappers/wmr_timestamp.h"
#include "ui/gfx/transform.h"
#include "ui/gfx/transform_util.h"

namespace device {

// We want to differentiate from gfx::Members, so we're not going to explicitly
// use anything from Windows::Foundation::Numerics
namespace WFN = ABI::Windows::Foundation::Numerics;

using Handedness =
    ABI::Windows::UI::Input::Spatial::SpatialInteractionSourceHandedness;
using PressKind = ABI::Windows::UI::Input::Spatial::SpatialInteractionPressKind;
using SourceKind =
    ABI::Windows::UI::Input::Spatial::SpatialInteractionSourceKind;

using ABI::Windows::Foundation::ITypedEventHandler;
using ABI::Windows::UI::Input::Spatial::ISpatialInteractionManager;
using ABI::Windows::UI::Input::Spatial::ISpatialInteractionSourceEventArgs;
using ABI::Windows::UI::Input::Spatial::ISpatialInteractionSourceEventArgs2;
using ABI::Windows::UI::Input::Spatial::ISpatialInteractionSourceState;
using ABI::Windows::UI::Input::Spatial::SpatialInteractionManager;
using ABI::Windows::UI::Input::Spatial::SpatialInteractionSourceEventArgs;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

typedef ITypedEventHandler<SpatialInteractionManager*,
                           SpatialInteractionSourceEventArgs*>
    SpatialInteractionSourceEventHandler;

ParsedInputState::ParsedInputState() {}
ParsedInputState::~ParsedInputState() {}
ParsedInputState::ParsedInputState(ParsedInputState&& other) = default;

namespace {
constexpr double kDeadzoneMinimum = 0.1;

void AddButton(mojom::XRGamepadPtr& gamepad, ButtonData* data) {
  if (data) {
    auto button = mojom::XRGamepadButton::New();
    button->pressed = data->pressed;
    button->touched = data->touched;
    button->value = data->value;

    gamepad->buttons.push_back(std::move(button));
  } else {
    gamepad->buttons.push_back(mojom::XRGamepadButton::New());
  }
}

// These methods are only called for the thumbstick and touchpad, which both
// have an X and Y.
void AddAxes(mojom::XRGamepadPtr& gamepad, ButtonData data) {
  gamepad->axes.push_back(
      std::fabs(data.x_axis) < kDeadzoneMinimum ? 0 : data.x_axis);
  gamepad->axes.push_back(
      std::fabs(data.y_axis) < kDeadzoneMinimum ? 0 : data.y_axis);
}

void AddButtonAndAxes(mojom::XRGamepadPtr& gamepad, ButtonData data) {
  AddButton(gamepad, &data);
  AddAxes(gamepad, data);
}

std::vector<float> ConvertToVector(GamepadVector vector) {
  return {vector.x, vector.y, vector.z};
}

std::vector<float> ConvertToVector(GamepadQuaternion quat) {
  return {quat.x, quat.y, quat.z, quat.w};
}

mojom::VRPosePtr ConvertToVRPose(GamepadPose gamepad_pose) {
  if (!gamepad_pose.not_null)
    return nullptr;

  auto pose = mojom::VRPose::New();
  if (gamepad_pose.orientation.not_null)
    pose->orientation = ConvertToVector(gamepad_pose.orientation);

  if (gamepad_pose.position.not_null)
    pose->position = ConvertToVector(gamepad_pose.position);

  if (gamepad_pose.angular_velocity.not_null)
    pose->angularVelocity = ConvertToVector(gamepad_pose.angular_velocity);

  if (gamepad_pose.linear_velocity.not_null)
    pose->linearVelocity = ConvertToVector(gamepad_pose.linear_velocity);

  if (gamepad_pose.angular_acceleration.not_null)
    pose->angularAcceleration =
        ConvertToVector(gamepad_pose.angular_acceleration);

  if (gamepad_pose.linear_acceleration.not_null)
    pose->linearAcceleration =
        ConvertToVector(gamepad_pose.linear_acceleration);

  return pose;
}

GamepadQuaternion ConvertToGamepadQuaternion(WFN::Quaternion quat) {
  GamepadQuaternion gamepad_quaternion;
  gamepad_quaternion.not_null = true;
  gamepad_quaternion.x = quat.X;
  gamepad_quaternion.y = quat.Y;
  gamepad_quaternion.z = quat.Z;
  gamepad_quaternion.w = quat.W;
  return gamepad_quaternion;
}

GamepadVector ConvertToGamepadVector(WFN::Vector3 vec3) {
  GamepadVector gamepad_vector;
  gamepad_vector.not_null = true;
  gamepad_vector.x = vec3.X;
  gamepad_vector.y = vec3.Y;
  gamepad_vector.z = vec3.Z;
  return gamepad_vector;
}

mojom::XRGamepadPtr GetWebVRGamepad(ParsedInputState input_state) {
  auto gamepad = mojom::XRGamepad::New();
  // This matches the order of button trigger events from Edge.  Note that we
  // use the polled button state for select here.  Voice (which we cannot get
  // via polling), lacks enough data to be considered a "Gamepad", and if we
  // used eventing the pressed state may be inconsistent.
  AddButtonAndAxes(gamepad, input_state.button_data[ButtonName::kThumbstick]);
  AddButton(gamepad, &input_state.button_data[ButtonName::kSelect]);
  AddButton(gamepad, &input_state.button_data[ButtonName::kGrip]);
  AddButton(gamepad, nullptr);  // Nothing seems to trigger this button in Edge.
  AddButtonAndAxes(gamepad, input_state.button_data[ButtonName::kTouchpad]);

  gamepad->pose = ConvertToVRPose(input_state.gamepad_pose);
  gamepad->hand = input_state.source_state->description->handedness;
  gamepad->controller_id = input_state.source_state->source_id;
  gamepad->can_provide_position = true;
  gamepad->can_provide_orientation = true;
  gamepad->timestamp = base::TimeTicks::Now();

  return gamepad;
}

// Note that since this is built by polling, and so eventing changes are not
// accounted for here.
std::unordered_map<ButtonName, ButtonData> ParseButtonState(
    const WMRInputSourceState& source_state) {
  std::unordered_map<ButtonName, ButtonData> button_map;

  ButtonData data = button_map[ButtonName::kSelect];
  data.pressed = source_state.IsSelectPressed();
  data.touched = data.pressed;
  data.value = source_state.SelectPressedValue();
  button_map[ButtonName::kSelect] = data;

  data = button_map[ButtonName::kGrip];
  data.pressed = source_state.IsGrasped();
  data.touched = data.pressed;
  data.value = data.pressed ? 1.0 : 0.0;
  button_map[ButtonName::kGrip] = data;

  if (!source_state.SupportsControllerProperties())
    return button_map;

  data = button_map[ButtonName::kThumbstick];
  data.pressed = source_state.IsThumbstickPressed();
  data.touched = data.pressed;
  data.value = data.pressed ? 1.0 : 0.0;

  data.x_axis = source_state.ThumbstickX();
  data.y_axis = source_state.ThumbstickY();
  button_map[ButtonName::kThumbstick] = data;

  data = button_map[ButtonName::kTouchpad];
  data.pressed = source_state.IsTouchpadPressed();
  data.touched = source_state.IsTouchpadTouched();
  data.value = data.pressed ? 1.0 : 0.0;

  // Touchpad must be touched if it is pressed
  if (data.pressed && !data.touched)
    data.touched = true;

  if (data.touched) {
    data.x_axis = source_state.TouchpadX();
    data.y_axis = source_state.TouchpadY();
  }

  button_map[ButtonName::kTouchpad] = data;

  return button_map;
}

GamepadPose GetGamepadPose(const WMRInputLocation& location) {
  GamepadPose gamepad_pose;

  WFN::Quaternion quat;
  if (location.TryGetOrientation(&quat)) {
    gamepad_pose.not_null = true;
    gamepad_pose.orientation = ConvertToGamepadQuaternion(quat);
  }

  WFN::Vector3 vec3;
  if (location.TryGetPosition(&vec3)) {
    gamepad_pose.not_null = true;
    gamepad_pose.position = ConvertToGamepadVector(vec3);
  }

  if (location.TryGetVelocity(&vec3)) {
    gamepad_pose.not_null = true;
    gamepad_pose.linear_velocity = ConvertToGamepadVector(vec3);
  }

  if (location.TryGetAngularVelocity(&vec3)) {
    gamepad_pose.not_null = true;
    gamepad_pose.angular_velocity = ConvertToGamepadVector(vec3);
  }

  return gamepad_pose;
}

gfx::Transform CreateTransform(GamepadVector position,
                               GamepadQuaternion rotation) {
  gfx::DecomposedTransform decomposed_transform;
  decomposed_transform.translate[0] = position.x;
  decomposed_transform.translate[1] = position.y;
  decomposed_transform.translate[2] = position.z;

  decomposed_transform.quaternion =
      gfx::Quaternion(rotation.x, rotation.y, rotation.z, rotation.w);
  return gfx::ComposeTransform(decomposed_transform);
}

bool TryGetPointerOffset(const WMRInputSourceState& state,
                         const WMRInputSource& source,
                         const WMRCoordinateSystem* origin,
                         gfx::Transform origin_from_grip,
                         gfx::Transform* grip_from_pointer) {
  DCHECK(grip_from_pointer);
  *grip_from_pointer = gfx::Transform();

  if (!origin)
    return false;

  // We can get the pointer position, but we'll need to transform it to an
  // offset from the grip position.  If we can't get an inverse of that,
  // then go ahead and bail early.
  gfx::Transform grip_from_origin;
  if (!origin_from_grip.GetInverse(&grip_from_origin))
    return false;

  bool pointing_supported = source.IsPointingSupported();

  WMRPointerPose pointer_pose(nullptr);
  if (!state.TryGetPointerPose(origin, &pointer_pose))
    return false;

  WFN::Vector3 pos;
  WFN::Quaternion rot;
  if (pointing_supported) {
    WMRPointerSourcePose pointer_source_pose(nullptr);
    if (!pointer_pose.TryGetInteractionSourcePose(source, &pointer_source_pose))
      return false;

    pos = pointer_source_pose.Position();
    rot = pointer_source_pose.Orientation();
  } else {
    pos = pointer_pose.HeadForward();
  }

  gfx::Transform origin_from_pointer = CreateTransform(
      ConvertToGamepadVector(pos), ConvertToGamepadQuaternion(rot));
  *grip_from_pointer = (grip_from_origin * origin_from_pointer);
  return true;
}

device::mojom::XRHandedness WindowsToMojoHandedness(Handedness handedness) {
  switch (handedness) {
    case Handedness::SpatialInteractionSourceHandedness_Left:
      return device::mojom::XRHandedness::LEFT;
    case Handedness::SpatialInteractionSourceHandedness_Right:
      return device::mojom::XRHandedness::RIGHT;
    default:
      return device::mojom::XRHandedness::NONE;
  }
}

uint32_t GetSourceId(const WMRInputSource& source) {
  uint32_t id = source.Id();

  // Voice's ID seems to be coming through as 0, which will cause a DCHECK in
  // the hash table used on the blink side.  To ensure that we don't have any
  // collisions with other ids, increment all of the ids by one.
  id++;
  DCHECK(id != 0);

  return id;
}
}  // namespace

MixedRealityInputHelper::MixedRealityInputHelper(HWND hwnd) : hwnd_(hwnd) {
  pressed_token_.value = 0;
  released_token_.value = 0;
}

MixedRealityInputHelper::~MixedRealityInputHelper() {
  // Dispose must be called before destruction, which ensures that we're
  // unsubscribed from events.
  DCHECK(pressed_token_.value == 0);
  DCHECK(released_token_.value == 0);
}

void MixedRealityInputHelper::Dispose() {
  UnsubscribeEvents();
}

bool MixedRealityInputHelper::EnsureSpatialInteractionManager() {
  if (input_manager_)
    return true;

  if (!hwnd_)
    return false;

  input_manager_ = WMRInputManager::GetForWindow(hwnd_);

  if (!input_manager_)
    return false;

  SubscribeEvents();
  return true;
}

std::vector<mojom::XRInputSourceStatePtr>
MixedRealityInputHelper::GetInputState(const WMRCoordinateSystem* origin,
                                       const WMRTimestamp* timestamp) {
  std::vector<mojom::XRInputSourceStatePtr> input_states;

  if (!timestamp || !origin || !EnsureSpatialInteractionManager())
    return input_states;

  base::AutoLock scoped_lock(lock_);
  auto source_states =
      input_manager_->GetDetectedSourcesAtTimestamp(timestamp->GetRawPtr());
  for (auto state : source_states) {
    auto parsed_source_state = LockedParseWindowsSourceState(state, origin);

    if (parsed_source_state.source_state)
      input_states.push_back(std::move(parsed_source_state.source_state));
  }

  for (unsigned int i = 0; i < pending_voice_states_.size(); i++) {
    auto parsed_source_state =
        LockedParseWindowsSourceState(pending_voice_states_[i], origin);

    if (parsed_source_state.source_state)
      input_states.push_back(std::move(parsed_source_state.source_state));
  }

  pending_voice_states_.clear();

  return input_states;
}

mojom::XRGamepadDataPtr MixedRealityInputHelper::GetWebVRGamepadData(
    const WMRCoordinateSystem* origin,
    const WMRTimestamp* timestamp) {
  auto ret = mojom::XRGamepadData::New();

  if (!timestamp || !origin || !EnsureSpatialInteractionManager())
    return ret;

  base::AutoLock scoped_lock(lock_);
  auto source_states =
      input_manager_->GetDetectedSourcesAtTimestamp(timestamp->GetRawPtr());
  for (auto state : source_states) {
    auto parsed_source_state = LockedParseWindowsSourceState(state, origin);

    // If we have a grip, then we should have enough data.
    if (parsed_source_state.source_state &&
        parsed_source_state.source_state->grip)
      ret->gamepads.push_back(GetWebVRGamepad(std::move(parsed_source_state)));
  }

  return ret;
}

ParsedInputState MixedRealityInputHelper::LockedParseWindowsSourceState(
    const WMRInputSourceState& state,
    const WMRCoordinateSystem* origin) {
  ParsedInputState input_state;
  if (!origin)
    return input_state;

  WMRInputSource source = state.GetSource();
  SourceKind source_kind = source.Kind();

  bool is_controller =
      (source_kind == SourceKind::SpatialInteractionSourceKind_Controller);
  bool is_voice =
      (source_kind == SourceKind::SpatialInteractionSourceKind_Voice);

  if (!(is_controller || is_voice))
    return input_state;

  // Note that if this is from voice input, we're not supposed to send up the
  // "grip" position, so this will be left as identity and let us still use
  // the same code paths, as any transformations by it will leave the original
  // item unaffected.
  gfx::Transform origin_from_grip;
  if (is_controller) {
    input_state.button_data = ParseButtonState(state);
    WMRInputLocation location_in_origin(nullptr);
    if (!state.TryGetLocation(origin, &location_in_origin))
      return input_state;

    auto gamepad_pose = GetGamepadPose(location_in_origin);
    if (!(gamepad_pose.not_null && gamepad_pose.position.not_null &&
          gamepad_pose.orientation.not_null))
      return input_state;

    origin_from_grip =
        CreateTransform(gamepad_pose.position, gamepad_pose.orientation);
    input_state.gamepad_pose = gamepad_pose;
  }

  gfx::Transform grip_from_pointer;
  if (!TryGetPointerOffset(state, source, origin, origin_from_grip,
                           &grip_from_pointer))
    return input_state;

  // Now that we know we have tracking for the object, we'll start building.
  device::mojom::XRInputSourceStatePtr source_state =
      device::mojom::XRInputSourceState::New();

  // Hands may not have the same id especially if they are lost but since we
  // are only tracking controllers/voice, this id should be consistent.
  uint32_t id = GetSourceId(source);

  source_state->source_id = id;
  source_state->primary_input_pressed = controller_states_[id].pressed;
  source_state->primary_input_clicked = controller_states_[id].clicked;
  controller_states_[id].clicked = false;

  // Grip position should *only* be specified for a controller.
  if (is_controller) {
    source_state->grip = origin_from_grip;
  }

  device::mojom::XRInputSourceDescriptionPtr description =
      device::mojom::XRInputSourceDescription::New();

  // If we've gotten this far we've gotten the real position.
  description->emulated_position = false;
  description->pointer_offset = grip_from_pointer;

  if (is_voice) {
    description->target_ray_mode = device::mojom::XRTargetRayMode::GAZING;
    description->handedness = device::mojom::XRHandedness::NONE;
  } else if (is_controller) {
    description->target_ray_mode = device::mojom::XRTargetRayMode::POINTING;
    description->handedness = WindowsToMojoHandedness(source.Handedness());
  } else {
    NOTREACHED();
  }

  source_state->description = std::move(description);

  input_state.source_state = std::move(source_state);

  return input_state;
}

HRESULT MixedRealityInputHelper::OnSourcePressed(
    ISpatialInteractionManager* sender,
    ISpatialInteractionSourceEventArgs* args) {
  return ProcessSourceEvent(args, true /* is_pressed */);
}

HRESULT MixedRealityInputHelper::OnSourceReleased(
    ISpatialInteractionManager* sender,
    ISpatialInteractionSourceEventArgs* args) {
  return ProcessSourceEvent(args, false /* is_pressed */);
}

HRESULT MixedRealityInputHelper::ProcessSourceEvent(
    ISpatialInteractionSourceEventArgs* raw_args,
    bool is_pressed) {
  base::AutoLock scoped_lock(lock_);
  ComPtr<ISpatialInteractionSourceEventArgs> args(raw_args);
  ComPtr<ISpatialInteractionSourceEventArgs2> args2;
  HRESULT hr = args.As(&args2);
  if (FAILED(hr))
    return S_OK;

  PressKind press_kind;
  hr = args2->get_PressKind(&press_kind);
  DCHECK(SUCCEEDED(hr));

  if (press_kind != PressKind::SpatialInteractionPressKind_Select)
    return S_OK;

  ComPtr<ISpatialInteractionSourceState> source_state_wmr;
  hr = args->get_State(&source_state_wmr);
  DCHECK(SUCCEEDED(hr));

  WMRInputSourceState state(source_state_wmr);
  WMRInputSource source = state.GetSource();

  SourceKind source_kind = source.Kind();

  if (source_kind != SourceKind::SpatialInteractionSourceKind_Controller &&
      source_kind != SourceKind::SpatialInteractionSourceKind_Voice)
    return S_OK;

  uint32_t id = GetSourceId(source);

  bool wasPressed = controller_states_[id].pressed;
  bool wasClicked = controller_states_[id].clicked;
  controller_states_[id].pressed = is_pressed;
  controller_states_[id].clicked = wasClicked || (wasPressed && !is_pressed);

  // Tracked controllers show up when we poll for DetectedSources, but voice
  // does not.
  if (source_kind == SourceKind::SpatialInteractionSourceKind_Voice &&
      !is_pressed)
    pending_voice_states_.push_back(std::move(state));
  return S_OK;
}

void MixedRealityInputHelper::SubscribeEvents() {
  DCHECK(input_manager_);
  DCHECK(pressed_token_.value == 0);
  DCHECK(released_token_.value == 0);

  // The destructor ensures that we're unsubscribed so raw this is fine.
  auto pressed_callback = Callback<SpatialInteractionSourceEventHandler>(
      this, &MixedRealityInputHelper::OnSourcePressed);
  HRESULT hr = input_manager_->GetComPtr()->add_SourcePressed(
      pressed_callback.Get(), &pressed_token_);
  DCHECK(SUCCEEDED(hr));

  // The destructor ensures that we're unsubscribed so raw this is fine.
  auto released_callback = Callback<SpatialInteractionSourceEventHandler>(
      this, &MixedRealityInputHelper::OnSourceReleased);
  hr = input_manager_->GetComPtr()->add_SourceReleased(released_callback.Get(),
                                                       &released_token_);
  DCHECK(SUCCEEDED(hr));
}

void MixedRealityInputHelper::UnsubscribeEvents() {
  base::AutoLock scoped_lock(lock_);
  if (!input_manager_)
    return;

  HRESULT hr = S_OK;
  if (pressed_token_.value != 0) {
    hr = input_manager_->GetComPtr()->remove_SourcePressed(pressed_token_);
    pressed_token_.value = 0;
    DCHECK(SUCCEEDED(hr));
  }

  if (released_token_.value != 0) {
    hr = input_manager_->GetComPtr()->remove_SourceReleased(released_token_);
    released_token_.value = 0;
    DCHECK(SUCCEEDED(hr));
  }
}

}  // namespace device
