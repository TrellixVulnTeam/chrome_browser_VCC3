// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SCHEDULER_BROWSER_UI_THREAD_SCHEDULER_H_
#define CONTENT_BROWSER_SCHEDULER_BROWSER_UI_THREAD_SCHEDULER_H_

#include <memory>

#include "base/containers/flat_map.h"
#include "base/message_loop/message_loop.h"
#include "base/task/sequence_manager/task_queue.h"
#include "content/browser/scheduler/browser_ui_thread_task_queue.h"
#include "content/common/content_export.h"

namespace base {
namespace sequence_manager {
class SequenceManager;
class TimeDomain;
}  // namespace sequence_manager
}  // namespace base

namespace content {
class BrowserTaskExecutor;

// The BrowserUIThreadScheduler vends TaskQueues and manipulates them to
// implement scheduling policy. This class is never deleted in production.
class CONTENT_EXPORT BrowserUIThreadScheduler {
 public:
  BrowserUIThreadScheduler();
  ~BrowserUIThreadScheduler();

  // Setting the DefaultTaskRunner is up to the caller.
  static std::unique_ptr<BrowserUIThreadScheduler> CreateForTesting(
      base::sequence_manager::SequenceManager* sequence_manager,
      base::sequence_manager::TimeDomain* time_domain);

  // Initializes any scheduler experiments.
  void PostFeatureListSetup();

  // Releases the scheduler although GetTaskRunner() continues to operate
  // however no tasks will be executed after this point.
  void Shutdown();

  void EnableBestEffortQueues();

  using QueueType = BrowserUIThreadTaskQueue::QueueType;

  // Can be called from any thread.
  scoped_refptr<base::SingleThreadTaskRunner> GetTaskRunnerForTesting(
      QueueType queue_type);

  // Adds a fence to all queues, runs all tasks until idle, and finally removes
  // the fences. Note that the run loop will eventually become idle, as new
  // tasks will not be scheduled due to the fence.
  void RunAllPendingTasksForTesting();

 private:
  friend class BrowserTaskExecutor;

  BrowserUIThreadScheduler(
      base::sequence_manager::SequenceManager* sequence_manager,
      base::sequence_manager::TimeDomain* time_domain);

  // Note this will be called before the FeatureList has been initialized.
  void InitialiseTaskQueues();

  // Creates all well known task queues (BrowserUIThreadTaskQueue::QueueType).
  void CreateTaskQueuesAndRunners();

  void InitialiseBestEffortQueue();

  scoped_refptr<base::SingleThreadTaskRunner> GetTaskRunner(
      QueueType queue_type);

  // In production the BrowserUIThreadScheduler will own its SequenceManager,
  // but in tests it may not.
  std::unique_ptr<base::sequence_manager::SequenceManager>
      owned_sequence_manager_;

  base::sequence_manager::SequenceManager* sequence_manager_;
  base::sequence_manager::TimeDomain* time_domain_;

  // The |task_queues_| and |task_runners_| are eagerly constructed and are
  // immutable after InitialiseTaskQueues() has run. If we ever change that e.g.
  // for per-frame scheduling then we will need to protect this with a lock.
  // NB |task_runners_| outlive the SequenceManager, but |task_queues_| do not.
  base::flat_map<QueueType, scoped_refptr<BrowserUIThreadTaskQueue>>
      task_queues_;
  base::flat_map<QueueType, scoped_refptr<base::SingleThreadTaskRunner>>
      task_runners_;

  std::unique_ptr<base::sequence_manager::TaskQueue::QueueEnabledVoter>
      best_effort_voter_;

  DISALLOW_COPY_AND_ASSIGN(BrowserUIThreadScheduler);
};

}  // namespace content

#endif  // CONTENT_BROWSER_SCHEDULER_BROWSER_UI_THREAD_SCHEDULER_H_
