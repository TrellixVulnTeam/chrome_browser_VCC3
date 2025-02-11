// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_ASSISTANT_ASSISTANT_ALARM_TIMER_CONTROLLER_H_
#define ASH_ASSISTANT_ASSISTANT_ALARM_TIMER_CONTROLLER_H_

#include "ash/assistant/assistant_controller_observer.h"
#include "ash/assistant/model/assistant_alarm_timer_model.h"
#include "ash/assistant/model/assistant_alarm_timer_model_observer.h"
#include "ash/public/interfaces/assistant_controller.mojom.h"
#include "base/macros.h"
#include "base/timer/timer.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace ash {

namespace assistant {
namespace util {
enum class AlarmTimerAction;
}  // namespace util
}  // namespace assistant

class AssistantController;

// The AssistantAlarmTimerController is a sub-controller of AssistantController
// tasked with tracking alarm/timer state and providing alarm/timer APIs.
class AssistantAlarmTimerController
    : public mojom::AssistantAlarmTimerController,
      public AssistantControllerObserver,
      public AssistantAlarmTimerModelObserver {
 public:
  explicit AssistantAlarmTimerController(
      AssistantController* assistant_controller);
  ~AssistantAlarmTimerController() override;

  void BindRequest(mojom::AssistantAlarmTimerControllerRequest request);

  // Returns the underlying model.
  const AssistantAlarmTimerModel* model() const { return &model_; }

  // Adds/removes the specified model |observer|.
  void AddModelObserver(AssistantAlarmTimerModelObserver* observer);
  void RemoveModelObserver(AssistantAlarmTimerModelObserver* observer);

  // mojom::AssistantAlarmTimerController:
  void OnTimerSoundingStarted() override;
  void OnTimerSoundingFinished() override;
  void OnAlarmTimerStateChanged(
      mojom::AssistantAlarmTimerEventPtr event) override;

  // AssistantAlarmTimerModelObserver:
  void OnAlarmTimerAdded(const AlarmTimer& alarm_timer,
                         const base::TimeDelta& time_remaining) override;
  void OnAlarmsTimersTicked(
      const std::map<std::string, base::TimeDelta>& times_remaining) override;
  void OnAllAlarmsTimersRemoved() override;

  // Provides a pointer to the |assistant| owned by AssistantController.
  void SetAssistant(chromeos::assistant::mojom::Assistant* assistant);

  // AssistantControllerObserver:
  void OnDeepLinkReceived(
      assistant::util::DeepLinkType type,
      const std::map<std::string, std::string>& params) override;

 private:
  void PerformAlarmTimerAction(
      const assistant::util::AlarmTimerAction& action,
      const base::Optional<std::string>& alarm_timer_id,
      const base::Optional<base::TimeDelta>& duration);

  AssistantController* const assistant_controller_;  // Owned by Shell.

  mojo::Binding<mojom::AssistantAlarmTimerController> binding_;

  AssistantAlarmTimerModel model_;

  base::RepeatingTimer timer_;

  // Owned by AssistantController.
  chromeos::assistant::mojom::Assistant* assistant_;

  int next_timer_id_ = 1;

  DISALLOW_COPY_AND_ASSIGN(AssistantAlarmTimerController);
};

}  // namespace ash

#endif  // ASH_ASSISTANT_ASSISTANT_ALARM_TIMER_CONTROLLER_H_
