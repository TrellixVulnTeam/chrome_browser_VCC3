// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/power/ml/smart_dim/model_impl.h"

#include <memory>

#include "base/bind.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/scoped_task_environment.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/power/ml/user_activity_event.pb.h"
#include "chromeos/constants/chromeos_features.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/page_transition_types.h"

namespace chromeos {
namespace power {
namespace ml {

namespace {

UserActivityEvent::Features DefaultFeatures() {
  UserActivityEvent::Features features;
  // Bucketize to 95.
  features.set_battery_percent(96.0);
  features.set_device_management(UserActivityEvent::Features::UNMANAGED);
  features.set_device_mode(UserActivityEvent::Features::CLAMSHELL);
  features.set_device_type(UserActivityEvent::Features::CHROMEBOOK);
  // Bucketize to 200.
  features.set_key_events_in_last_hour(290);
  features.set_last_activity_day(UserActivityEvent::Features::THU);
  // Bucketize to 7.
  features.set_last_activity_time_sec(25920);
  // Bucketize to 7.
  features.set_last_user_activity_time_sec(25920);
  // Bucketize to 2000.
  features.set_mouse_events_in_last_hour(2600);
  features.set_on_battery(false);
  features.set_previous_negative_actions_count(3);
  features.set_previous_positive_actions_count(0);
  features.set_recent_time_active_sec(190);
  features.set_video_playing_time_sec(0);
  features.set_on_to_dim_sec(30);
  features.set_dim_to_screen_off_sec(10);
  features.set_time_since_last_key_sec(30);
  features.set_time_since_last_mouse_sec(688);
  // Bucketize to 900.
  features.set_time_since_video_ended_sec(1100);
  features.set_has_form_entry(false);
  features.set_source_id(123);  // not used.
  features.set_engagement_score(40);
  features.set_tab_domain("//mail.google.com");
  return features;
}

// Class to hold scoped local modifications to Smart-dim related feature flags.
class SmartDimFeatureFlags {
 public:
  SmartDimFeatureFlags() = default;

  void Initialize(const bool use_ml_service, const double dim_threshold) {
    const std::map<std::string, std::string> params = {
        {"dim_threshold", base::NumberToString(dim_threshold)}};
    smart_dim_feature_override_.InitAndEnableFeatureWithParameters(
        features::kUserActivityPrediction, params);

    if (use_ml_service) {
      ml_service_feature_override_.InitAndEnableFeature(
          features::kUserActivityPredictionMlService);
    } else {
      ml_service_feature_override_.InitAndDisableFeature(
          features::kUserActivityPredictionMlService);
    }
  }

 private:
  base::test::ScopedFeatureList smart_dim_feature_override_;
  base::test::ScopedFeatureList ml_service_feature_override_;

  DISALLOW_COPY_AND_ASSIGN(SmartDimFeatureFlags);
};

class FakeMlServiceClient : public MlServiceClient {
 public:
  FakeMlServiceClient() = default;
  ~FakeMlServiceClient() override {}

  // MlServiceClient:
  void DoInference(
      const std::vector<float>& features,
      base::RepeatingCallback<UserActivityEvent::ModelPrediction(float)>
          get_prediction_callback,
      SmartDimModel::DimDecisionCallback decision_callback) override {
    // Corresponds to DefaultFeatures() with TfNativeModel:
    const float inactivity_score = -3.6615708;

    UserActivityEvent::ModelPrediction model_prediction =
        get_prediction_callback.Run(inactivity_score);
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(decision_callback), model_prediction));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(FakeMlServiceClient);
};

}  // namespace

// Parameterized test fixture. The bool parameter is whether to test the ML
// Service codepath. If false, the old TFNative codepath is tested.
class SmartDimModelImplTest : public testing::TestWithParam<bool> {
 public:
  SmartDimModelImplTest()
      : scoped_task_environment_(
            base::test::ScopedTaskEnvironment::MainThreadType::IO,
            base::test::ScopedTaskEnvironment::ThreadPoolExecutionMode::
                QUEUED) {}

  ~SmartDimModelImplTest() override = default;

 protected:
  // More readable name for the test parameter:
  bool UsesMlService() const { return GetParam(); }

  // Sets a fake MlServiceClient into |impl|.
  void InjectFakeMlServiceClient(SmartDimModelImpl* const impl) {
    impl->SetMlServiceClientForTesting(std::make_unique<FakeMlServiceClient>());
  }

  base::test::ScopedTaskEnvironment scoped_task_environment_;

 private:
  DISALLOW_COPY_AND_ASSIGN(SmartDimModelImplTest);
};

// For the TFNative model, test a hard-coded known-good model result.
TEST(SmartDimTfNativeModelTest, Basic) {
  base::test::ScopedTaskEnvironment env;
  SmartDimFeatureFlags flags;
  flags.Initialize(false /* use_ml_service */, -0.1 /* dim_threshold */);

  SmartDimModelImpl smart_dim_model;

  float inactivity_score;
  EXPECT_EQ(SmartDimModelResult::kSuccess,
            smart_dim_model.CalculateInactivityScoreTfNative(
                DefaultFeatures(), &inactivity_score));
  // Score has been calculated outside of chrome.
  EXPECT_FLOAT_EQ(-3.6615708, inactivity_score);
}

// For the TFNative model, test a known-good result with empty features.
TEST(SmartDimTfNativeModelTest, OptionalFeaturesMissing) {
  base::test::ScopedTaskEnvironment env;
  SmartDimFeatureFlags flags;
  flags.Initialize(false /* use_ml_service */, -0.1 /* dim_threshold */);

  SmartDimModelImpl smart_dim_model;

  UserActivityEvent::Features features = DefaultFeatures();
  features.clear_battery_percent();
  features.clear_time_since_last_key_sec();
  features.clear_time_since_last_mouse_sec();
  features.clear_time_since_video_ended_sec();
  features.clear_source_id();
  features.clear_has_form_entry();
  features.clear_engagement_score();
  features.clear_tab_domain();

  float inactivity_score;
  EXPECT_EQ(SmartDimModelResult::kSuccess,
            smart_dim_model.CalculateInactivityScoreTfNative(
                features, &inactivity_score));
  // Score has been calculated outside of chrome.
  EXPECT_FLOAT_EQ(-1.9680425, inactivity_score);
}

TEST_P(SmartDimModelImplTest, ShouldNotDim) {
  SmartDimFeatureFlags flags;
  flags.Initialize(UsesMlService(), -0.1 /* dim_threshold */);

  SmartDimModelImpl smart_dim_model;
  if (UsesMlService())
    InjectFakeMlServiceClient(&smart_dim_model);

  bool callback_done = false;
  smart_dim_model.RequestDimDecision(
      DefaultFeatures(), base::BindOnce(
                             [](bool* callback_done,
                                UserActivityEvent::ModelPrediction prediction) {
                               EXPECT_EQ(
                                   UserActivityEvent::ModelPrediction::NO_DIM,
                                   prediction.response());
                               EXPECT_EQ(47, prediction.decision_threshold());
                               EXPECT_EQ(2, prediction.inactivity_score());
                               *callback_done = true;
                             },
                             &callback_done));
  scoped_task_environment_.RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

TEST_P(SmartDimModelImplTest, ShouldDim) {
  SmartDimFeatureFlags flags;
  flags.Initialize(UsesMlService(), -10.0 /* dim_threshold */);

  SmartDimModelImpl smart_dim_model;
  if (UsesMlService())
    InjectFakeMlServiceClient(&smart_dim_model);

  bool callback_done = false;
  smart_dim_model.RequestDimDecision(
      DefaultFeatures(), base::BindOnce(
                             [](bool* callback_done,
                                UserActivityEvent::ModelPrediction prediction) {
                               EXPECT_EQ(
                                   UserActivityEvent::ModelPrediction::DIM,
                                   prediction.response());
                               EXPECT_EQ(0, prediction.decision_threshold());
                               EXPECT_EQ(2, prediction.inactivity_score());
                               *callback_done = true;
                             },
                             &callback_done));
  scoped_task_environment_.RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

// Check that CancelableCallback ensures a callback doesn't execute twice, in
// case two RequestDimDecision() calls were made before any callback ran.
TEST_P(SmartDimModelImplTest, CheckCancelableCallback) {
  SmartDimFeatureFlags flags;
  flags.Initialize(UsesMlService(), -10.0 /* dim_threshold */);

  SmartDimModelImpl smart_dim_model;
  if (UsesMlService())
    InjectFakeMlServiceClient(&smart_dim_model);

  bool callback_done = false;
  int num_callbacks_run = 0;
  for (int i = 0; i < 2; i++) {
    smart_dim_model.RequestDimDecision(
        DefaultFeatures(),
        base::BindOnce(
            [](bool* callback_done, int* num_callbacks_run,
               UserActivityEvent::ModelPrediction prediction) {
              EXPECT_EQ(UserActivityEvent::ModelPrediction::DIM,
                        prediction.response());
              EXPECT_EQ(0, prediction.decision_threshold());
              EXPECT_EQ(2, prediction.inactivity_score());
              *callback_done = true;
              (*num_callbacks_run)++;
            },
            &callback_done, &num_callbacks_run));
  }
  scoped_task_environment_.RunUntilIdle();
  EXPECT_TRUE(callback_done);
  EXPECT_EQ(1, num_callbacks_run);
}

// Check that CancelPreviousRequest() can successfully prevent a previous
// requested dim decision request from running.
TEST_P(SmartDimModelImplTest, CheckCanceledRequest) {
  SmartDimFeatureFlags flags;
  flags.Initialize(UsesMlService(), -10.0 /* dim_threshold */);

  SmartDimModelImpl smart_dim_model;
  if (UsesMlService())
    InjectFakeMlServiceClient(&smart_dim_model);

  bool callback_done = false;
  smart_dim_model.RequestDimDecision(
      DefaultFeatures(), base::BindOnce(
                             [](bool* callback_done,
                                UserActivityEvent::ModelPrediction prediction) {
                               *callback_done = true;
                             },
                             &callback_done));
  smart_dim_model.CancelPreviousRequest();
  scoped_task_environment_.RunUntilIdle();
  EXPECT_FALSE(callback_done);
}

INSTANTIATE_TEST_SUITE_P(SmartDimModelImplTests,
                         SmartDimModelImplTest,
                         testing::Bool());

}  // namespace ml
}  // namespace power
}  // namespace chromeos
