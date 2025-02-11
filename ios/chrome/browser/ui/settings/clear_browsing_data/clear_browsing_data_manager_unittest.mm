// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/settings/clear_browsing_data/clear_browsing_data_manager.h"

#include "base/bind.h"
#include "components/browsing_data/core/pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/sync/driver/test_sync_service.h"
#include "components/sync_preferences/pref_service_mock_factory.h"
#include "components/sync_preferences/pref_service_syncable.h"
#include "ios/chrome/browser/application_context.h"
#include "ios/chrome/browser/browser_state/test_chrome_browser_state.h"
#include "ios/chrome/browser/browsing_data/browsing_data_features.h"
#include "ios/chrome/browser/browsing_data/cache_counter.h"
#include "ios/chrome/browser/browsing_data/fake_browsing_data_remover.h"
#include "ios/chrome/browser/pref_names.h"
#include "ios/chrome/browser/prefs/browser_prefs.h"
#include "ios/chrome/browser/signin/identity_test_environment_chrome_browser_state_adaptor.h"
#include "ios/chrome/browser/sync/profile_sync_service_factory.h"
#import "ios/chrome/browser/ui/settings/clear_browsing_data/fake_browsing_data_counter_wrapper_producer.h"
#import "ios/chrome/browser/ui/table_view/table_view_model.h"
#include "ios/web/public/test/test_web_thread_bundle.h"
#include "services/identity/public/cpp/identity_test_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#import "testing/gtest_mac.h"
#include "testing/platform_test.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

std::unique_ptr<KeyedService> CreateTestSyncService(
    web::BrowserState* context) {
  return std::make_unique<syncer::TestSyncService>();
}

class ClearBrowsingDataManagerTest : public PlatformTest {
 public:
  ClearBrowsingDataManagerTest() {
    sync_preferences::PrefServiceMockFactory factory;
    scoped_refptr<user_prefs::PrefRegistrySyncable> registry(
        new user_prefs::PrefRegistrySyncable);
    std::unique_ptr<sync_preferences::PrefServiceSyncable> prefs =
        factory.CreateSyncable(registry.get());
    RegisterBrowserStatePrefs(registry.get());

    TestChromeBrowserState::TestingFactories factories = {
        {ProfileSyncServiceFactory::GetInstance(),
         base::BindRepeating(&CreateTestSyncService)},
    };
    browser_state_ = IdentityTestEnvironmentChromeBrowserStateAdaptor::
        CreateChromeBrowserStateForIdentityTestEnvironment(factories);

    identity_test_env_adaptor_.reset(
        new IdentityTestEnvironmentChromeBrowserStateAdaptor(
            browser_state_.get()));

    model_ = [[TableViewModel alloc] init];
    remover_ = std::make_unique<FakeBrowsingDataRemover>();
    manager_ = [[ClearBrowsingDataManager alloc]
                      initWithBrowserState:browser_state_.get()
                                  listType:ClearBrowsingDataListType::
                                               kListTypeCollectionView
                       browsingDataRemover:remover_.get()
        browsingDataCounterWrapperProducer:
            [[FakeBrowsingDataCounterWrapperProducer alloc] init]];

    test_sync_service_ = static_cast<syncer::TestSyncService*>(
        ProfileSyncServiceFactory::GetForBrowserState(browser_state_.get()));
  }

  identity::IdentityTestEnvironment* identity_test_env() {
    return identity_test_env_adaptor_->identity_test_env();
  }

 protected:
  std::unique_ptr<TestChromeBrowserState> browser_state_;
  std::unique_ptr<IdentityTestEnvironmentChromeBrowserStateAdaptor>
      identity_test_env_adaptor_;
  TableViewModel* model_;
  std::unique_ptr<BrowsingDataRemover> remover_;
  ClearBrowsingDataManager* manager_;
  syncer::TestSyncService* test_sync_service_;
  web::TestWebThreadBundle thread_bundle_;
};

// Tests model is set up with all appropriate items and sections.
TEST_F(ClearBrowsingDataManagerTest, TestModel) {
  [manager_ loadModel:model_];

  int section_offset = 0;
  if (IsNewClearBrowsingDataUIEnabled()) {
    EXPECT_EQ(4, [model_ numberOfSections]);
    EXPECT_EQ(1, [model_ numberOfItemsInSection:0]);
    section_offset = 1;
  } else {
    EXPECT_EQ(3, [model_ numberOfSections]);
  }
  EXPECT_EQ(5, [model_ numberOfItemsInSection:0 + section_offset]);
  EXPECT_EQ(1, [model_ numberOfItemsInSection:1 + section_offset]);
  EXPECT_EQ(1, [model_ numberOfItemsInSection:2 + section_offset]);
}

// Tests model is set up with correct number of items and sections if signed in
// but sync is off.
TEST_F(ClearBrowsingDataManagerTest, TestModelSignedInSyncOff) {
  // Ensure that sync is not running.
  test_sync_service_->SetDisableReasons(
      syncer::SyncService::DISABLE_REASON_USER_CHOICE);

  identity_test_env()->SetPrimaryAccount("syncuser@example.com");

  [manager_ loadModel:model_];

  int section_offset = 0;
  if (IsNewClearBrowsingDataUIEnabled()) {
    EXPECT_EQ(5, [model_ numberOfSections]);
    EXPECT_EQ(1, [model_ numberOfItemsInSection:0]);
    section_offset = 1;
  } else {
    EXPECT_EQ(4, [model_ numberOfSections]);
  }

  EXPECT_EQ(5, [model_ numberOfItemsInSection:0 + section_offset]);
  EXPECT_EQ(1, [model_ numberOfItemsInSection:1 + section_offset]);
  EXPECT_EQ(1, [model_ numberOfItemsInSection:2 + section_offset]);
  EXPECT_EQ(1, [model_ numberOfItemsInSection:3 + section_offset]);
}

TEST_F(ClearBrowsingDataManagerTest, TestCacheCounterFormattingForAllTime) {
  ASSERT_EQ("en", GetApplicationContext()->GetApplicationLocale());
  PrefService* prefs = browser_state_->GetPrefs();
  prefs->SetInteger(browsing_data::prefs::kDeleteTimePeriod,
                    static_cast<int>(browsing_data::TimePeriod::ALL_TIME));
  CacheCounter counter(browser_state_.get());

  // Test multiple possible types of formatting.
  // clang-format off
    const struct TestCase {
        int cache_size;
        NSString* expected_output;
    } kTestCases[] = {
        {0, @"Less than 1 MB"},
        {(1 << 20) - 1, @"Less than 1 MB"},
        {(1 << 20), @"1 MB"},
        {(1 << 20) + (1 << 19), @"1.5 MB"},
        {(1 << 21), @"2 MB"},
        {(1 << 30), @"1 GB"}
    };
  // clang-format on

  for (const TestCase& test_case : kTestCases) {
    browsing_data::BrowsingDataCounter::FinishedResult result(
        &counter, test_case.cache_size);
    NSString* output = [manager_ counterTextFromResult:result];
    EXPECT_NSEQ(test_case.expected_output, output);
  }
}

TEST_F(ClearBrowsingDataManagerTest,
       TestCacheCounterFormattingForLessThanAllTime) {
  ASSERT_EQ("en", GetApplicationContext()->GetApplicationLocale());

  // If the new UI is not enabled then the pref value for the time period
  // is ignored and the time period defaults to ALL_TIME.
  if (!IsNewClearBrowsingDataUIEnabled()) {
    return;
  }
  PrefService* prefs = browser_state_->GetPrefs();
  prefs->SetInteger(browsing_data::prefs::kDeleteTimePeriod,
                    static_cast<int>(browsing_data::TimePeriod::LAST_HOUR));
  CacheCounter counter(browser_state_.get());

  // Test multiple possible types of formatting.
  // clang-format off
    const struct TestCase {
        int cache_size;
        NSString* expected_output;
    } kTestCases[] = {
        {0, @"Less than 1 MB"},
        {(1 << 20) - 1, @"Less than 1 MB"},
        {(1 << 20), @"Less than 1 MB"},
        {(1 << 20) + (1 << 19), @"Less than 1.5 MB"},
        {(1 << 21), @"Less than 2 MB"},
        {(1 << 30), @"Less than 1 GB"}
    };
  // clang-format on

  for (const TestCase& test_case : kTestCases) {
    browsing_data::BrowsingDataCounter::FinishedResult result(
        &counter, test_case.cache_size);
    NSString* output = [manager_ counterTextFromResult:result];
    EXPECT_NSEQ(test_case.expected_output, output);
  }
}

}  // namespace
