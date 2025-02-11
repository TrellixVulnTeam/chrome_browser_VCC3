// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/download/public/common/simple_download_manager_coordinator.h"

#include <utility>

#include "components/download/public/common/all_download_event_notifier.h"
#include "components/download/public/common/download_item.h"
#include "components/download/public/common/simple_download_manager.h"

namespace download {

SimpleDownloadManagerCoordinator::SimpleDownloadManagerCoordinator()
    : simple_download_manager_(nullptr),
      has_all_history_downloads_(false),
      current_manager_has_all_history_downloads_(false),
      initialized_(false),
      weak_factory_(this) {}

SimpleDownloadManagerCoordinator::~SimpleDownloadManagerCoordinator() {
  if (simple_download_manager_)
    simple_download_manager_->RemoveObserver(this);
  for (auto& observer : observers_)
    observer.OnManagerGoingDown();
}

void SimpleDownloadManagerCoordinator::SetSimpleDownloadManager(
    SimpleDownloadManager* simple_download_manager,
    bool manages_all_history_downloads) {
  DCHECK(simple_download_manager);
  // Make sure we won't transition from a full manager to a in-progress manager,
  DCHECK(!current_manager_has_all_history_downloads_ ||
         manages_all_history_downloads);

  if (simple_download_manager_)
    simple_download_manager_->RemoveObserver(this);
  current_manager_has_all_history_downloads_ = manages_all_history_downloads;
  simple_download_manager_ = simple_download_manager;
  simple_download_manager_->AddObserver(this);
}

void SimpleDownloadManagerCoordinator::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
  if (initialized_) {
    for (auto& observer : observers_)
      observer.OnDownloadsInitialized(!has_all_history_downloads_);
  }
}

void SimpleDownloadManagerCoordinator::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void SimpleDownloadManagerCoordinator::DownloadUrl(
    std::unique_ptr<DownloadUrlParameters> parameters) {
  if (simple_download_manager_)
    simple_download_manager_->DownloadUrl(std::move(parameters));
}

void SimpleDownloadManagerCoordinator::GetAllDownloads(
    std::vector<DownloadItem*>* downloads) {
  if (simple_download_manager_)
    simple_download_manager_->GetAllDownloads(downloads);
}

DownloadItem* SimpleDownloadManagerCoordinator::GetDownloadByGuid(
    const std::string& guid) {
  if (simple_download_manager_)
    return simple_download_manager_->GetDownloadByGuid(guid);
  return nullptr;
}

void SimpleDownloadManagerCoordinator::OnDownloadsInitialized() {
  initialized_ = true;
  has_all_history_downloads_ = current_manager_has_all_history_downloads_;
  for (auto& observer : observers_)
    observer.OnDownloadsInitialized(!has_all_history_downloads_);
}

void SimpleDownloadManagerCoordinator::OnManagerGoingDown() {
  simple_download_manager_ = nullptr;
}

void SimpleDownloadManagerCoordinator::OnDownloadCreated(DownloadItem* item) {
  for (auto& observer : observers_)
    observer.OnDownloadCreated(item);
}

AllDownloadEventNotifier* SimpleDownloadManagerCoordinator::GetNotifier() {
  if (!notifier_)
    notifier_ = std::make_unique<AllDownloadEventNotifier>(this);
  return notifier_.get();
}

}  // namespace download
