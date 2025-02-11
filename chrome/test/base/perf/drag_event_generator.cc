// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/base/perf/drag_event_generator.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/task/post_task.h"
#include "base/threading/thread_task_runner_handle.h"
#include "ui/base/test/ui_controls.h"

namespace ui_test_utils {
namespace {

// The frame duration for 60fps.
constexpr base::TimeDelta kFrameDuration =
    base::TimeDelta::FromMicroseconds(16666);

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// DragEventGenerator

DragEventGenerator::DragEventGenerator(std::unique_ptr<PointProducer> producer,
                                       bool touch)
    : producer_(std::move(producer)),
      start_(base::TimeTicks::Now()),
      expected_next_time_(start_ + kFrameDuration),
      touch_(touch) {
  gfx::Point initial_position = producer_->GetPosition(0.f);
  if (touch_) {
    ui_controls::SendTouchEvents(ui_controls::PRESS, 0, initial_position.x(),
                                 initial_position.y());
  } else {
    ui_controls::SendMouseMove(initial_position.x(), initial_position.y());
    ui_controls::SendMouseEvents(ui_controls::LEFT, ui_controls::DOWN);
  }
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&DragEventGenerator::GenerateNext, base::Unretained(this)),
      kFrameDuration);
}

DragEventGenerator::~DragEventGenerator() {
  VLOG(1) << "Effective Event per seconds="
          << (count_ * 1000) / producer_->GetDuration().InMilliseconds();
}

void DragEventGenerator::Wait() {
  run_loop_.Run();
}

void DragEventGenerator::GenerateNext() {
  auto now = base::TimeTicks::Now();
  auto elapsed = now - start_;

  expected_next_time_ += kFrameDuration;
  count_++;
  const base::TimeDelta duration = producer_->GetDuration();
  if (elapsed >= duration) {
    gfx::Point position = producer_->GetPosition(1.0f);
    if (touch_) {
      ui_controls::SendTouchEventsNotifyWhenDone(
          ui_controls::MOVE, 0, position.x(), position.y(),
          base::BindOnce(&DragEventGenerator::Done, base::Unretained(this),
                         position));
    } else {
      ui_controls::SendMouseMoveNotifyWhenDone(
          position.x(), position.y(),
          base::BindOnce(&DragEventGenerator::Done, base::Unretained(this),
                         position));
    }
    return;
  }

  float progress =
      static_cast<float>(elapsed.InMilliseconds()) / duration.InMilliseconds();
  gfx::Point position = producer_->GetPosition(progress);
  if (touch_) {
    ui_controls::SendTouchEvents(ui_controls::MOVE, 0, position.x(),
                                 position.y());
  } else {
    ui_controls::SendMouseMove(position.x(), position.y());
  }

  auto delta = expected_next_time_ - now;
  if (delta.InMilliseconds() > 0) {
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&DragEventGenerator::GenerateNext,
                       base::Unretained(this)),
        delta);
  } else {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(&DragEventGenerator::GenerateNext,
                                  base::Unretained(this)));
  }
}

void DragEventGenerator::Done(const gfx::Point position) {
  if (touch_) {
    ui_controls::SendTouchEventsNotifyWhenDone(ui_controls::RELEASE, 0,
                                               position.x(), position.y(),
                                               run_loop_.QuitClosure());
  } else {
    ui_controls::SendMouseEventsNotifyWhenDone(
        ui_controls::LEFT, ui_controls::UP, run_loop_.QuitClosure());
  }
}

////////////////////////////////////////////////////////////////////////////////
// DragEventGenerator::PointProducer

DragEventGenerator::PointProducer::~PointProducer() = default;

////////////////////////////////////////////////////////////////////////////////
// InterporateProducer:

InterporateProducer::InterporateProducer(const gfx::Point& start,
                                         const gfx::Point& end,
                                         const base::TimeDelta duration,
                                         gfx::Tween::Type type)
    : start_(start), end_(end), duration_(duration), type_(type) {}

InterporateProducer::~InterporateProducer() = default;

gfx::Point InterporateProducer::GetPosition(float progress) {
  float value = gfx::Tween::CalculateValue(type_, progress);
  return gfx::Point(
      gfx::Tween::LinearIntValueBetween(value, start_.x(), end_.x()),
      gfx::Tween::LinearIntValueBetween(value, start_.y(), end_.y()));
}

const base::TimeDelta InterporateProducer::GetDuration() const {
  return duration_;
}

}  // namespace ui_test_utils
