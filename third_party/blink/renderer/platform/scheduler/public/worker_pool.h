// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_SCHEDULER_PUBLIC_WORKER_POOL_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_SCHEDULER_PUBLIC_WORKER_POOL_H_

#include "base/location.h"
#include "base/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "third_party/blink/renderer/platform/platform_export.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"

namespace blink {

namespace worker_pool {

// These are a thin wrapper around base::ThreadPool to ensure that all
// callers use WTF::CrossThreadBind instead of base::Bind to ensure that
// all non-thread-safe objects are copied properly.
//
// All tasks that do not care about which thread they are running on
// (e.g. compressing/uncompressing tasks) use this API.
//
// Tasks that have to run on a specific thread (e.g. main thread, compositor
// thread, dedicated worker thread) should be posted via other means
// (e.g. FrameScheduler for main thread tasks).
PLATFORM_EXPORT void PostTask(const base::Location&, CrossThreadClosure);

PLATFORM_EXPORT void PostTaskWithTraits(const base::Location&,
                                        const base::TaskTraits&,
                                        CrossThreadClosure);

// TODO(altimin): Expose CreateSequencedTaskRunnerWithTraits when the
// need arises.

}  // namespace worker_pool

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_SCHEDULER_PUBLIC_WORKER_POOL_H_
