// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/overlays/overlay_request_queue_impl.h"

#include <utility>

#include "base/memory/ptr_util.h"
#include "ios/chrome/browser/overlays/public/overlay_request.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

#pragma mark - Factory method

OverlayRequestQueue* OverlayRequestQueue::FromWebState(
    web::WebState* web_state,
    OverlayModality modality) {
  OverlayRequestQueueImpl::Container::CreateForWebState(web_state);
  return OverlayRequestQueueImpl::Container::FromWebState(web_state)
      ->QueueForModality(modality);
}

#pragma mark - OverlayRequestQueueImpl::Container

WEB_STATE_USER_DATA_KEY_IMPL(OverlayRequestQueueImpl::Container)

OverlayRequestQueueImpl::Container::Container(web::WebState* web_state) {}
OverlayRequestQueueImpl::Container::~Container() = default;

OverlayRequestQueueImpl* OverlayRequestQueueImpl::Container::QueueForModality(
    OverlayModality modality) {
  auto& queue = queues_[modality];
  if (!queue)
    queue = base::WrapUnique(new OverlayRequestQueueImpl());
  return queue.get();
}

#pragma mark - OverlayRequestQueueImpl

OverlayRequestQueueImpl::OverlayRequestQueueImpl() = default;
OverlayRequestQueueImpl::~OverlayRequestQueueImpl() = default;

void OverlayRequestQueueImpl::PopRequest() {
  DCHECK(!requests_.empty());
  std::unique_ptr<OverlayRequest> popped_request = std::move(requests_.front());
  requests_.pop_front();
  for (auto& observer : observers_) {
    observer.OnRequestRemoved(this, popped_request.get());
  }
}

void OverlayRequestQueueImpl::AddRequest(
    std::unique_ptr<OverlayRequest> request) {
  requests_.push_back(std::move(request));
  for (auto& observer : observers_) {
    observer.OnRequestAdded(this, requests_.back().get());
  }
}

OverlayRequest* OverlayRequestQueueImpl::front_request() const {
  return requests_.empty() ? nullptr : requests_.front().get();
}
