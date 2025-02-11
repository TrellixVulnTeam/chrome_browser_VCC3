// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/tracing/public/cpp/perfetto/perfetto_traced_process.h"

#include "base/no_destructor.h"
#include "base/task/post_task.h"
#include "base/task/thread_pool/scheduler_lock_impl.h"
#include "services/tracing/public/cpp/perfetto/producer_client.h"

namespace tracing {

PerfettoTracedProcess::DataSourceBase::DataSourceBase(const std::string& name)
    : name_(name) {
  DCHECK(!name.empty());
}

PerfettoTracedProcess::DataSourceBase::~DataSourceBase() = default;

void PerfettoTracedProcess::DataSourceBase::StartTracingWithID(
    uint64_t data_source_id,
    PerfettoProducer* producer_client,
    const perfetto::DataSourceConfig& data_source_config) {
  data_source_id_ = data_source_id;
  StartTracing(producer_client, data_source_config);
}

// static
PerfettoTracedProcess* PerfettoTracedProcess::Get() {
  static base::NoDestructor<PerfettoTracedProcess> traced_process;
  return traced_process.get();
}

PerfettoTracedProcess::PerfettoTracedProcess()
    : producer_client_(new ProducerClient(GetTaskRunner())),
      weak_ptr_factory_(this) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

PerfettoTracedProcess::~PerfettoTracedProcess() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

ProducerClient* PerfettoTracedProcess::SetProducerClientForTesting(
    ProducerClient* client) {
  data_sources_.clear();
  auto* old_producer_client_for_testing = producer_client_;
  producer_client_ = client;
  return old_producer_client_for_testing;
}

// static
void PerfettoTracedProcess::DeleteSoonForTesting(
    std::unique_ptr<PerfettoTracedProcess> perfetto_traced_process) {
  GetTaskRunner()->GetOrCreateTaskRunner()->DeleteSoon(
      FROM_HERE, std::move(perfetto_traced_process));
}

// We never destroy the taskrunner as we may need it for cleanup
// of TraceWriters in TLS, which could happen after the PerfettoTracedProcess
// is deleted.
// static
PerfettoTaskRunner* PerfettoTracedProcess::GetTaskRunner() {
  static base::NoDestructor<PerfettoTaskRunner> task_runner(nullptr);
  return task_runner.get();
}

// static
void PerfettoTracedProcess::ResetTaskRunnerForTesting() {
  DETACH_FROM_SEQUENCE(PerfettoTracedProcess::Get()->sequence_checker_);
  GetTaskRunner()->ResetTaskRunnerForTesting(nullptr);
}

void PerfettoTracedProcess::AddDataSource(DataSourceBase* data_source) {
  GetTaskRunner()->GetOrCreateTaskRunner()->PostTask(
      FROM_HERE, base::BindOnce(&PerfettoTracedProcess::AddDataSourceOnSequence,
                                base::Unretained(this), data_source));
}

ProducerClient* PerfettoTracedProcess::producer_client() {
  return producer_client_;
}

void PerfettoTracedProcess::AddDataSourceOnSequence(
    DataSourceBase* data_source) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (data_sources_.insert(data_source).second) {
    producer_client_->NewDataSourceAdded(data_source);
  }
}
}  // namespace tracing
