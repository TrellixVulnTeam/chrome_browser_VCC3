// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_BLUETOOTH_BLUETOOTH_REMOTE_GATT_CHARACTERISTIC_WINRT_H_
#define DEVICE_BLUETOOTH_BLUETOOTH_REMOTE_GATT_CHARACTERISTIC_WINRT_H_

#include <windows.devices.bluetooth.genericattributeprofile.h>
#include <wrl/client.h>

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/macros.h"
#include "device/bluetooth/bluetooth_export.h"
#include "device/bluetooth/bluetooth_remote_gatt_characteristic.h"
#include "device/bluetooth/bluetooth_uuid.h"

namespace device {

class BluetoothRemoteGattDescriptor;
class BluetoothGattDiscovererWinrt;
class BluetoothRemoteGattService;

class DEVICE_BLUETOOTH_EXPORT BluetoothRemoteGattCharacteristicWinrt
    : public BluetoothRemoteGattCharacteristic {
 public:
  static std::unique_ptr<BluetoothRemoteGattCharacteristicWinrt> Create(
      BluetoothRemoteGattService* service,
      Microsoft::WRL::ComPtr<ABI::Windows::Devices::Bluetooth::
                                 GenericAttributeProfile::IGattCharacteristic>
          characteristic);
  ~BluetoothRemoteGattCharacteristicWinrt() override;

  // BluetoothGattCharacteristic:
  std::string GetIdentifier() const override;
  BluetoothUUID GetUUID() const override;
  Properties GetProperties() const override;
  Permissions GetPermissions() const override;

  // BluetoothRemoteGattCharacteristic:
  const std::vector<uint8_t>& GetValue() const override;
  BluetoothRemoteGattService* GetService() const override;
  void ReadRemoteCharacteristic(ValueCallback callback,
                                ErrorCallback error_callback) override;
  void WriteRemoteCharacteristic(const std::vector<uint8_t>& value,
                                 base::OnceClosure callback,
                                 ErrorCallback error_callback) override;
  bool WriteWithoutResponse(base::span<const uint8_t> value) override;

  void UpdateDescriptors(BluetoothGattDiscovererWinrt* gatt_discoverer);

  ABI::Windows::Devices::Bluetooth::GenericAttributeProfile::
      IGattCharacteristic*
      GetCharacteristicForTesting();

 protected:
  // BluetoothRemoteGattCharacteristic:
  void SubscribeToNotifications(BluetoothRemoteGattDescriptor* ccc_descriptor,
                                const base::Closure& callback,
                                ErrorCallback error_callback) override;
  void UnsubscribeFromNotifications(
      BluetoothRemoteGattDescriptor* ccc_descriptor,
      const base::Closure& callback,
      ErrorCallback error_callback) override;

 private:
  struct PendingReadCallbacks {
    PendingReadCallbacks(ValueCallback callback, ErrorCallback error_callback);
    ~PendingReadCallbacks();

    ValueCallback callback;
    ErrorCallback error_callback;
  };

  struct PendingWriteCallbacks {
    PendingWriteCallbacks(base::OnceClosure callback,
                          ErrorCallback error_callback);
    ~PendingWriteCallbacks();

    base::OnceClosure callback;
    ErrorCallback error_callback;
  };

  using PendingNotificationCallbacks = PendingWriteCallbacks;

  BluetoothRemoteGattCharacteristicWinrt(
      BluetoothRemoteGattService* service,
      Microsoft::WRL::ComPtr<ABI::Windows::Devices::Bluetooth::
                                 GenericAttributeProfile::IGattCharacteristic>
          characteristic,
      BluetoothUUID uuid,
      Properties proporties,
      uint16_t attribute_handle);

  void WriteCccDescriptor(
      ABI::Windows::Devices::Bluetooth::GenericAttributeProfile::
          GattClientCharacteristicConfigurationDescriptorValue value,
      base::OnceClosure callback,
      ErrorCallback error_callback);

  void OnReadValue(Microsoft::WRL::ComPtr<
                   ABI::Windows::Devices::Bluetooth::GenericAttributeProfile::
                       IGattReadResult> read_result);

  void OnWriteValueWithResultAndOption(
      Microsoft::WRL::ComPtr<ABI::Windows::Devices::Bluetooth::
                                 GenericAttributeProfile::IGattWriteResult>
          write_result);

  void OnWriteCccDescriptor(
      Microsoft::WRL::ComPtr<ABI::Windows::Devices::Bluetooth::
                                 GenericAttributeProfile::IGattWriteResult>
          write_result);

  void OnWriteImpl(
      Microsoft::WRL::ComPtr<ABI::Windows::Devices::Bluetooth::
                                 GenericAttributeProfile::IGattWriteResult>
          write_result,
      std::unique_ptr<PendingWriteCallbacks> callbacks);

  void OnValueChanged(
      ABI::Windows::Devices::Bluetooth::GenericAttributeProfile::
          IGattCharacteristic* characteristic,
      ABI::Windows::Devices::Bluetooth::GenericAttributeProfile::
          IGattValueChangedEventArgs* event_args);

  bool RemoveValueChangedHandler();

  BluetoothRemoteGattService* service_;
  Microsoft::WRL::ComPtr<ABI::Windows::Devices::Bluetooth::
                             GenericAttributeProfile::IGattCharacteristic>
      characteristic_;
  BluetoothUUID uuid_;
  Properties properties_;
  uint16_t attribute_handle_;
  std::string identifier_;
  std::vector<uint8_t> value_;
  std::unique_ptr<PendingReadCallbacks> pending_read_callbacks_;
  std::unique_ptr<PendingWriteCallbacks> pending_write_callbacks_;
  std::unique_ptr<PendingNotificationCallbacks> pending_notification_callbacks_;
  base::Optional<EventRegistrationToken> value_changed_token_;

  base::WeakPtrFactory<BluetoothRemoteGattCharacteristicWinrt>
      weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(BluetoothRemoteGattCharacteristicWinrt);
};

}  // namespace device

#endif  // DEVICE_BLUETOOTH_BLUETOOTH_REMOTE_GATT_CHARACTERISTIC_WINRT_H_
