// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/audio/stream_factory.h"

#include <utility>

#include "base/bind.h"
#include "base/strings/stringprintf.h"
#include "base/trace_event/trace_event.h"
#include "base/unguessable_token.h"
#include "components/crash/core/common/crash_key.h"
#include "services/audio/input_stream.h"
#include "services/audio/local_muter.h"
#include "services/audio/loopback_stream.h"
#include "services/audio/output_stream.h"
#include "services/audio/user_input_monitor.h"

namespace audio {

StreamFactory::StreamFactory(media::AudioManager* audio_manager)
    : audio_manager_(audio_manager) {
  magic_bytes_ = 0x600DC0DEu;
  SetStateForCrashing("constructed");
}

StreamFactory::~StreamFactory() {
  CHECK_EQ(magic_bytes_, 0x600DC0DEu);
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  SetStateForCrashing("destructing");
  magic_bytes_ = 0xDEADBEEFu;
}

void StreamFactory::Bind(mojo::PendingReceiver<mojom::StreamFactory> receiver,
                         TracedServiceRef context_ref) {
  CHECK_EQ(magic_bytes_, 0x600DC0DEu);
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  receivers_.Add(this, std::move(receiver), std::move(context_ref));
}

void StreamFactory::CreateInputStream(
    mojo::PendingReceiver<media::mojom::AudioInputStream> stream_receiver,
    mojo::PendingRemote<media::mojom::AudioInputStreamClient> client,
    mojo::PendingRemote<media::mojom::AudioInputStreamObserver> observer,
    mojo::PendingRemote<media::mojom::AudioLog> pending_log,
    const std::string& device_id,
    const media::AudioParameters& params,
    uint32_t shared_memory_count,
    bool enable_agc,
    mojo::ScopedSharedBufferHandle key_press_count_buffer,
    mojom::AudioProcessingConfigPtr processing_config,
    CreateInputStreamCallback created_callback) {
  CHECK_EQ(magic_bytes_, 0x600DC0DEu);
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  SetStateForCrashing("creating input stream");
  TRACE_EVENT_NESTABLE_ASYNC_INSTANT2(
      "audio", "CreateInputStream", receivers_.current_context().id_for_trace(),
      "device id", device_id, "params", params.AsHumanReadableString());

  if (processing_config && processing_config->settings.requires_apm() &&
      params.GetBufferDuration() != base::TimeDelta::FromMilliseconds(10)) {
    // If the buffer size is incorrect, the data can't be fed into the APM.
    // This should never happen unless a renderer misbehaves.
    mojo::Remote<media::mojom::AudioLog> log(std::move(pending_log));
    log->OnLogMessage("Invalid APM config.");
    log->OnError();
    // The callback must still be invoked or mojo complains.
    std::move(created_callback).Run(nullptr, false, base::nullopt);
    SetStateForCrashing("input stream create failed");
    return;
  }

  // Unretained is safe since |this| indirectly owns the InputStream.
  auto deleter_callback = base::BindOnce(&StreamFactory::DestroyInputStream,
                                         base::Unretained(this));

  input_streams_.insert(std::make_unique<InputStream>(
      std::move(created_callback), std::move(deleter_callback),
      std::move(stream_receiver), std::move(client), std::move(observer),
      std::move(pending_log), audio_manager_,
      UserInputMonitor::Create(std::move(key_press_count_buffer)), device_id,
      params, shared_memory_count, enable_agc, &stream_monitor_coordinator_,
      std::move(processing_config)));
  SetStateForCrashing("created input stream");
}

void StreamFactory::AssociateInputAndOutputForAec(
    const base::UnguessableToken& input_stream_id,
    const std::string& output_device_id) {
  CHECK_EQ(magic_bytes_, 0x600DC0DEu);
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  SetStateForCrashing("associating for AEC");
  for (const auto& stream : input_streams_) {
    if (stream->id() == input_stream_id) {
      stream->SetOutputDeviceForAec(output_device_id);
      SetStateForCrashing("associated for AEC");
      return;
    }
  }
  SetStateForCrashing("did not associate for AEC");
}

void StreamFactory::CreateOutputStream(
    mojo::PendingReceiver<media::mojom::AudioOutputStream> stream_receiver,
    mojo::PendingAssociatedRemote<media::mojom::AudioOutputStreamObserver>
        observer,
    mojo::PendingRemote<media::mojom::AudioLog> log,
    const std::string& output_device_id,
    const media::AudioParameters& params,
    const base::UnguessableToken& group_id,
    const base::Optional<base::UnguessableToken>& processing_id,
    CreateOutputStreamCallback created_callback) {
  CHECK_EQ(magic_bytes_, 0x600DC0DEu);
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  SetStateForCrashing("creating output stream");
  TRACE_EVENT_NESTABLE_ASYNC_INSTANT2(
      "audio", "CreateOutputStream",
      receivers_.current_context().id_for_trace(), "device id",
      output_device_id, "params", params.AsHumanReadableString());

  // Unretained is safe since |this| indirectly owns the OutputStream.
  auto deleter_callback = base::BindOnce(&StreamFactory::DestroyOutputStream,
                                         base::Unretained(this));

  // This is required for multizone audio playback on Cast devices.
  // See //chromecast/media/cast_audio_manager.h for more information.
  const std::string device_id_or_group_id =
#if defined(IS_CHROMECAST)
      (group_id.ToString().empty()) ? output_device_id : group_id.ToString();
#else
      output_device_id;
#endif

  output_streams_.insert(std::make_unique<OutputStream>(
      std::move(created_callback), std::move(deleter_callback),
      std::move(stream_receiver), std::move(observer), std::move(log),
      audio_manager_, device_id_or_group_id, params, &coordinator_, group_id,
      &stream_monitor_coordinator_,
      processing_id.value_or(base::UnguessableToken())));
  SetStateForCrashing("created output stream");
}

void StreamFactory::BindMuter(
    mojo::PendingAssociatedReceiver<mojom::LocalMuter> receiver,
    const base::UnguessableToken& group_id) {
  CHECK_EQ(magic_bytes_, 0x600DC0DEu);
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  SetStateForCrashing("binding muter");
  TRACE_EVENT_NESTABLE_ASYNC_INSTANT1(
      "audio", "BindMuter", receivers_.current_context().id_for_trace(),
      "group id", group_id.GetLowForSerialization());

  // Find the existing LocalMuter for this group, or create one on-demand.
  auto it = std::find_if(muters_.begin(), muters_.end(),
                         [&group_id](const std::unique_ptr<LocalMuter>& muter) {
                           return muter->group_id() == group_id;
                         });
  LocalMuter* muter;
  if (it == muters_.end()) {
    auto muter_ptr = std::make_unique<LocalMuter>(&coordinator_, group_id);
    muter = muter_ptr.get();
    muter->SetAllBindingsLostCallback(base::BindOnce(
        &StreamFactory::DestroyMuter, base::Unretained(this), muter));
    muters_.emplace_back(std::move(muter_ptr));
  } else {
    muter = it->get();
  }

  // Add the receiver.
  muter->AddReceiver(std::move(receiver));
  SetStateForCrashing("bound muter");
}

void StreamFactory::CreateLoopbackStream(
    mojo::PendingReceiver<media::mojom::AudioInputStream> receiver,
    mojo::PendingRemote<media::mojom::AudioInputStreamClient> client,
    mojo::PendingRemote<media::mojom::AudioInputStreamObserver> observer,
    const media::AudioParameters& params,
    uint32_t shared_memory_count,
    const base::UnguessableToken& group_id,
    CreateLoopbackStreamCallback created_callback) {
  CHECK_EQ(magic_bytes_, 0x600DC0DEu);
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  SetStateForCrashing("creating loopback stream");
  TRACE_EVENT_NESTABLE_ASYNC_INSTANT2(
      "audio", "CreateLoopbackStream",
      receivers_.current_context().id_for_trace(), "group id",
      group_id.GetLowForSerialization(), "params",
      params.AsHumanReadableString());

  auto stream = std::make_unique<LoopbackStream>(
      std::move(created_callback),
      base::BindOnce(&StreamFactory::DestroyLoopbackStream,
                     base::Unretained(this)),
      audio_manager_->GetWorkerTaskRunner(), std::move(receiver),
      std::move(client), std::move(observer), params, shared_memory_count,
      &coordinator_, group_id);
  loopback_streams_.emplace_back(std::move(stream));
  SetStateForCrashing("created loopback stream");
}

void StreamFactory::DestroyInputStream(InputStream* stream) {
  CHECK_EQ(magic_bytes_, 0x600DC0DEu);
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  SetStateForCrashing("destroying input stream");
  size_t erased = input_streams_.erase(stream);
  DCHECK_EQ(1u, erased);
  SetStateForCrashing("destroyed input stream");
}

void StreamFactory::DestroyOutputStream(OutputStream* stream) {
  CHECK_EQ(magic_bytes_, 0x600DC0DEu);
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  SetStateForCrashing("destroying output stream");
  size_t erased = output_streams_.erase(stream);
  DCHECK_EQ(1u, erased);
  SetStateForCrashing("destroyed output stream");
}

void StreamFactory::DestroyMuter(LocalMuter* muter) {
  CHECK_EQ(magic_bytes_, 0x600DC0DEu);
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  DCHECK(muter);
  SetStateForCrashing("destroying muter");

  const auto it = std::find_if(muters_.begin(), muters_.end(),
                               base::MatchesUniquePtr(muter));
  DCHECK(it != muters_.end());
  muters_.erase(it);
  SetStateForCrashing("destroyed muter");
}

void StreamFactory::DestroyLoopbackStream(LoopbackStream* stream) {
  CHECK_EQ(magic_bytes_, 0x600DC0DEu);
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  DCHECK(stream);

  SetStateForCrashing("destroying loopback stream");

  const auto it =
      std::find_if(loopback_streams_.begin(), loopback_streams_.end(),
                   base::MatchesUniquePtr(stream));
  DCHECK(it != loopback_streams_.end());
  loopback_streams_.erase(it);

  SetStateForCrashing("destroyed loopback stream");
}

void StreamFactory::SetStateForCrashing(const char* state) {
  static crash_reporter::CrashKeyString<256> crash_string(
      "audio-service-factory-state");
  crash_string.Set(base::StringPrintf(
      "%s: binding_count=%d, muters_count=%d, loopback_count=%d, "
      "input_stream_count=%d, output_stream_count=%d",
      state, static_cast<int>(receivers_.size()),
      static_cast<int>(muters_.size()),
      static_cast<int>(loopback_streams_.size()),
      static_cast<int>(input_streams_.size()),
      static_cast<int>(output_streams_.size())));
}

}  // namespace audio
