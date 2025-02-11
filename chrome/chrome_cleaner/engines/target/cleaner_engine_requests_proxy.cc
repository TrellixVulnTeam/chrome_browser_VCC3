// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/chrome_cleaner/engines/target/cleaner_engine_requests_proxy.h"

#include <utility>
#include <vector>

#include "base/location.h"
#include "chrome/chrome_cleaner/engines/target/engine_commands_impl.h"
#include "chrome/chrome_cleaner/strings/string16_embedded_nulls.h"

namespace chrome_cleaner {

namespace {

void SaveBoolCallback(bool* out_result,
                      base::OnceClosure quit_closure,
                      bool result) {
  *out_result = result;
  std::move(quit_closure).Run();
}

}  // namespace

CleanerEngineRequestsProxy::CleanerEngineRequestsProxy(
    mojom::CleanerEngineRequestsAssociatedPtr requests_ptr,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : requests_ptr_(std::move(requests_ptr)), task_runner_(task_runner) {}

void CleanerEngineRequestsProxy::UnbindRequestsPtr() {
  requests_ptr_.reset();
}

bool CleanerEngineRequestsProxy::DeleteFile(const base::FilePath& file_name) {
  bool result = false;
  MojoCallStatus call_status = SyncSandboxRequest(
      this,
      base::BindOnce(&CleanerEngineRequestsProxy::SandboxDeleteFile,
                     base::Unretained(this), file_name),
      base::BindOnce(&SaveBoolCallback, &result));
  if (call_status.state == MojoCallStatus::MOJO_CALL_ERROR) {
    return false;
  }
  return result;
}

bool CleanerEngineRequestsProxy::DeleteFilePostReboot(
    const base::FilePath& file_name) {
  bool result = false;
  MojoCallStatus call_status = SyncSandboxRequest(
      this,
      base::BindOnce(&CleanerEngineRequestsProxy::SandboxDeleteFilePostReboot,
                     base::Unretained(this), file_name),
      base::BindOnce(&SaveBoolCallback, &result));
  if (call_status.state == MojoCallStatus::MOJO_CALL_ERROR) {
    return false;
  }
  return result;
}

bool CleanerEngineRequestsProxy::NtDeleteRegistryKey(
    const String16EmbeddedNulls& key) {
  bool result = false;
  MojoCallStatus call_status = SyncSandboxRequest(
      this,
      base::BindOnce(&CleanerEngineRequestsProxy::SandboxNtDeleteRegistryKey,
                     base::Unretained(this), key),
      base::BindOnce(&SaveBoolCallback, &result));
  if (call_status.state == MojoCallStatus::MOJO_CALL_ERROR) {
    return false;
  }
  return result;
}

bool CleanerEngineRequestsProxy::NtDeleteRegistryValue(
    const String16EmbeddedNulls& key,
    const String16EmbeddedNulls& value_name) {
  bool result = false;
  MojoCallStatus call_status = SyncSandboxRequest(
      this,
      base::BindOnce(&CleanerEngineRequestsProxy::SandboxNtDeleteRegistryValue,
                     base::Unretained(this), key, value_name),
      base::BindOnce(&SaveBoolCallback, &result));
  if (call_status.state == MojoCallStatus::MOJO_CALL_ERROR) {
    return false;
  }
  return result;
}

bool CleanerEngineRequestsProxy::NtChangeRegistryValue(
    const String16EmbeddedNulls& key,
    const String16EmbeddedNulls& value_name,
    const String16EmbeddedNulls& new_value) {
  bool result = false;
  MojoCallStatus call_status = SyncSandboxRequest(
      this,
      base::BindOnce(&CleanerEngineRequestsProxy::SandboxNtChangeRegistryValue,
                     base::Unretained(this), key, value_name, new_value),
      base::BindOnce(&SaveBoolCallback, &result));
  if (call_status.state == MojoCallStatus::MOJO_CALL_ERROR) {
    return false;
  }
  return result;
}

bool CleanerEngineRequestsProxy::DeleteService(const base::string16& name) {
  bool result = false;
  MojoCallStatus call_status = SyncSandboxRequest(
      this,
      base::BindOnce(&CleanerEngineRequestsProxy::SandboxDeleteService,
                     base::Unretained(this), name),
      base::BindOnce(&SaveBoolCallback, &result));
  if (call_status.state == MojoCallStatus::MOJO_CALL_ERROR) {
    return false;
  }
  return result;
}

bool CleanerEngineRequestsProxy::DeleteTask(const base::string16& name) {
  bool result = false;
  MojoCallStatus call_status = SyncSandboxRequest(
      this,
      base::BindOnce(&CleanerEngineRequestsProxy::SandboxDeleteTask,
                     base::Unretained(this), name),
      base::BindOnce(&SaveBoolCallback, &result));
  if (call_status.state == MojoCallStatus::MOJO_CALL_ERROR) {
    return false;
  }
  return result;
}

bool CleanerEngineRequestsProxy::TerminateProcess(base::ProcessId process_id) {
  bool result = false;
  MojoCallStatus call_status = SyncSandboxRequest(
      this,
      base::BindOnce(&CleanerEngineRequestsProxy::SandboxTerminateProcess,
                     base::Unretained(this), process_id),
      base::BindOnce(&SaveBoolCallback, &result));
  if (call_status.state == MojoCallStatus::MOJO_CALL_ERROR) {
    return false;
  }
  return result;
}

CleanerEngineRequestsProxy::~CleanerEngineRequestsProxy() = default;

MojoCallStatus CleanerEngineRequestsProxy::SandboxDeleteFile(
    const base::FilePath& path,
    mojom::CleanerEngineRequests::SandboxDeleteFileCallback result_callback) {
  if (!requests_ptr_.is_bound()) {
    LOG(ERROR) << "SandboxDeleteFile called without bound pointer";
    return MojoCallStatus::Failure(SandboxErrorCode::INTERNAL_ERROR);
  }

  requests_ptr_->SandboxDeleteFile(path, std::move(result_callback));
  return MojoCallStatus::Success();
}

MojoCallStatus CleanerEngineRequestsProxy::SandboxDeleteFilePostReboot(
    const base::FilePath& path,
    mojom::CleanerEngineRequests::SandboxDeleteFilePostRebootCallback
        result_callback) {
  if (!requests_ptr_.is_bound()) {
    LOG(ERROR) << "SandboxDeleteFilePostReboot called without bound pointer";
    return MojoCallStatus::Failure(SandboxErrorCode::INTERNAL_ERROR);
  }

  requests_ptr_->SandboxDeleteFilePostReboot(path, std::move(result_callback));
  return MojoCallStatus::Success();
}

MojoCallStatus CleanerEngineRequestsProxy::SandboxNtDeleteRegistryKey(
    const String16EmbeddedNulls& key,
    mojom::CleanerEngineRequests::SandboxNtDeleteRegistryKeyCallback
        result_callback) {
  if (!requests_ptr_.is_bound()) {
    LOG(ERROR) << "SandboxNtDeleteRegistryKey called without bound pointer";
    return MojoCallStatus::Failure(SandboxErrorCode::INTERNAL_ERROR);
  }

  requests_ptr_->SandboxNtDeleteRegistryKey(key, std::move(result_callback));
  return MojoCallStatus::Success();
}

MojoCallStatus CleanerEngineRequestsProxy::SandboxNtDeleteRegistryValue(
    const String16EmbeddedNulls& key,
    const String16EmbeddedNulls& value_name,
    mojom::CleanerEngineRequests::SandboxNtDeleteRegistryValueCallback
        result_callback) {
  if (!requests_ptr_.is_bound()) {
    LOG(ERROR) << "SandboxNtDeleteRegistryValue called without bound pointer";
    return MojoCallStatus::Failure(SandboxErrorCode::INTERNAL_ERROR);
  }

  requests_ptr_->SandboxNtDeleteRegistryValue(key, value_name,
                                              std::move(result_callback));
  return MojoCallStatus::Success();
}

MojoCallStatus CleanerEngineRequestsProxy::SandboxNtChangeRegistryValue(
    const String16EmbeddedNulls& key,
    const String16EmbeddedNulls& value_name,
    const String16EmbeddedNulls& new_value,
    mojom::CleanerEngineRequests::SandboxNtChangeRegistryValueCallback
        result_callback) {
  if (!requests_ptr_.is_bound()) {
    LOG(ERROR) << "SandboxNtChangeRegistryValue called without bound pointer";
    return MojoCallStatus::Failure(SandboxErrorCode::INTERNAL_ERROR);
  }

  requests_ptr_->SandboxNtChangeRegistryValue(key, value_name, new_value,
                                              std::move(result_callback));

  return MojoCallStatus::Success();
}

MojoCallStatus CleanerEngineRequestsProxy::SandboxDeleteService(
    const base::string16& name,
    mojom::CleanerEngineRequests::SandboxDeleteServiceCallback
        result_callback) {
  if (!requests_ptr_.is_bound()) {
    LOG(ERROR) << "SandboxDeleteService called without bound pointer";
    return MojoCallStatus::Failure(SandboxErrorCode::INTERNAL_ERROR);
  }

  requests_ptr_->SandboxDeleteService(name, std::move(result_callback));
  return MojoCallStatus::Success();
}

MojoCallStatus CleanerEngineRequestsProxy::SandboxDeleteTask(
    const base::string16& name,
    mojom::CleanerEngineRequests::SandboxDeleteTaskCallback result_callback) {
  if (!requests_ptr_.is_bound()) {
    LOG(ERROR) << "SandboxDeleteTask called without bound pointer";
    return MojoCallStatus::Failure(SandboxErrorCode::INTERNAL_ERROR);
  }

  requests_ptr_->SandboxDeleteTask(name, std::move(result_callback));
  return MojoCallStatus::Success();
}

MojoCallStatus CleanerEngineRequestsProxy::SandboxTerminateProcess(
    uint32_t process_id,
    mojom::CleanerEngineRequests::SandboxTerminateProcessCallback
        result_callback) {
  if (!requests_ptr_.is_bound()) {
    LOG(ERROR) << "SandboxTerminateProcess called without bound pointer";
    return MojoCallStatus::Failure(SandboxErrorCode::INTERNAL_ERROR);
  }

  requests_ptr_->SandboxTerminateProcess(process_id,
                                         std::move(result_callback));

  return MojoCallStatus::Success();
}

}  // namespace chrome_cleaner
