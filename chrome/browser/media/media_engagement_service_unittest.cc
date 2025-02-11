// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/media_engagement_service.h"

#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/simple_test_clock.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/media/media_engagement_score.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "components/content_settings/core/browser/content_settings_observer.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/history/core/browser/history_database_params.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/test/test_history_database.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "media/base/media_switches.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

base::FilePath g_temp_history_dir;

// History is automatically expired after 90 days.
base::TimeDelta kHistoryExpirationThreshold = base::TimeDelta::FromDays(90);

// Waits until a change is observed in media engagement content settings.
class MediaEngagementChangeWaiter : public content_settings::Observer {
 public:
  explicit MediaEngagementChangeWaiter(Profile* profile) : profile_(profile) {
    HostContentSettingsMapFactory::GetForProfile(profile)->AddObserver(this);
  }

  ~MediaEngagementChangeWaiter() override {
    HostContentSettingsMapFactory::GetForProfile(profile_)->RemoveObserver(
        this);
  }

  // Overridden from content_settings::Observer:
  void OnContentSettingChanged(
      const ContentSettingsPattern& primary_pattern,
      const ContentSettingsPattern& secondary_pattern,
      ContentSettingsType content_type,
      const std::string& resource_identifier) override {
    if (content_type == CONTENT_SETTINGS_TYPE_MEDIA_ENGAGEMENT)
      Proceed();
  }

  void Wait() { run_loop_.Run(); }

 private:
  void Proceed() { run_loop_.Quit(); }

  Profile* profile_;
  base::RunLoop run_loop_;

  DISALLOW_COPY_AND_ASSIGN(MediaEngagementChangeWaiter);
};

base::Time GetReferenceTime() {
  base::Time::Exploded exploded_reference_time;
  exploded_reference_time.year = 2015;
  exploded_reference_time.month = 1;
  exploded_reference_time.day_of_month = 30;
  exploded_reference_time.day_of_week = 5;
  exploded_reference_time.hour = 11;
  exploded_reference_time.minute = 0;
  exploded_reference_time.second = 0;
  exploded_reference_time.millisecond = 0;

  base::Time out_time;
  EXPECT_TRUE(
      base::Time::FromLocalExploded(exploded_reference_time, &out_time));
  return out_time;
}

std::unique_ptr<KeyedService> BuildTestHistoryService(
    scoped_refptr<base::SequencedTaskRunner> backend_runner,
    content::BrowserContext* context) {
  std::unique_ptr<history::HistoryService> service(
      new history::HistoryService());
  if (backend_runner)
    service->set_backend_task_runner_for_testing(std::move(backend_runner));
  service->Init(history::TestHistoryDatabaseParamsForPath(g_temp_history_dir));
  return service;
}

}  // namespace

class MediaEngagementServiceTest : public ChromeRenderViewHostTestHarness {
 public:
  void SetUp() override {
    mock_time_task_runner_ =
        base::MakeRefCounted<base::TestMockTimeTaskRunner>();
    scoped_feature_list_.InitAndEnableFeature(
        media::kRecordMediaEngagementScores);
    ChromeRenderViewHostTestHarness::SetUp();

    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    g_temp_history_dir = temp_dir_.GetPath();
    ConfigureHistoryService(nullptr);

    test_clock_.SetNow(GetReferenceTime());
    service_ = base::WrapUnique(StartNewMediaEngagementService());
  }

  MediaEngagementService* service() const { return service_.get(); }

  MediaEngagementService* StartNewMediaEngagementService() {
    MediaEngagementService* service =
        new MediaEngagementService(profile(), &test_clock_);
    base::RunLoop().RunUntilIdle();
    return service;
  }

  void ConfigureHistoryService(
      scoped_refptr<base::SequencedTaskRunner> backend_runner) {
    HistoryServiceFactory::GetInstance()->SetTestingFactory(
        profile(), base::BindRepeating(&BuildTestHistoryService,
                                       std::move(backend_runner)));
  }

  void RestartHistoryService(
      scoped_refptr<base::SequencedTaskRunner> backend_runner) {
    history::HistoryService* history_old = HistoryServiceFactory::GetForProfile(
        profile(), ServiceAccessType::IMPLICIT_ACCESS);
    history_old->Shutdown();

    HistoryServiceFactory::ShutdownForProfile(profile());
    ConfigureHistoryService(std::move(backend_runner));
    history::HistoryService* history = HistoryServiceFactory::GetForProfile(
        profile(), ServiceAccessType::IMPLICIT_ACCESS);
    history->AddObserver(service());
  }

  void RecordVisitAndPlaybackAndAdvanceClock(const url::Origin& origin) {
    RecordVisit(origin);
    AdvanceClock();
    RecordPlayback(origin);
  }

  void TearDown() override {
    service_->Shutdown();
    ChromeRenderViewHostTestHarness::TearDown();
    service_.reset();
  }

  void AdvanceClock() {
    test_clock_.SetNow(Now() + base::TimeDelta::FromHours(1));
  }

  void RecordVisit(const url::Origin& origin) { service_->RecordVisit(origin); }

  void RecordPlayback(const url::Origin& origin) {
    RecordPlaybackForService(service_.get(), origin);
  }

  void RecordPlaybackForService(MediaEngagementService* service,
                                const url::Origin& origin) {
    MediaEngagementScore score = service->CreateEngagementScore(origin);
    score.IncrementMediaPlaybacks();
    score.set_last_media_playback_time(service->clock()->Now());
    score.Commit();
  }

  void ExpectScores(MediaEngagementService* service,
                    const url::Origin& origin,
                    double expected_score,
                    int expected_visits,
                    int expected_media_playbacks,
                    base::Time expected_last_media_playback_time) {
    EXPECT_EQ(service->GetEngagementScore(origin), expected_score);
    EXPECT_EQ(service->GetScoreMapForTesting()[origin], expected_score);

    MediaEngagementScore score = service->CreateEngagementScore(origin);
    EXPECT_EQ(expected_visits, score.visits());
    EXPECT_EQ(expected_media_playbacks, score.media_playbacks());
    EXPECT_EQ(expected_last_media_playback_time,
              score.last_media_playback_time());
  }

  void ExpectScores(const url::Origin& origin,
                    double expected_score,
                    int expected_visits,
                    int expected_media_playbacks,
                    base::Time expected_last_media_playback_time) {
    ExpectScores(service_.get(), origin, expected_score, expected_visits,
                 expected_media_playbacks, expected_last_media_playback_time);
  }

  void SetScores(const url::Origin& origin, int visits, int media_playbacks) {
    MediaEngagementScore score = service_->CreateEngagementScore(origin);
    score.SetVisits(visits);
    score.SetMediaPlaybacks(media_playbacks);
    score.Commit();
  }

  void SetLastMediaPlaybackTime(const url::Origin& origin,
                                base::Time last_media_playback_time) {
    MediaEngagementScore score = service_->CreateEngagementScore(origin);
    score.last_media_playback_time_ = last_media_playback_time;
    score.Commit();
  }

  double GetActualScore(const url::Origin& origin) {
    return service_->CreateEngagementScore(origin).actual_score();
  }

  std::map<url::Origin, double> GetScoreMapForTesting() const {
    return service_->GetScoreMapForTesting();
  }

  void ClearDataBetweenTime(base::Time begin, base::Time end) {
    service_->ClearDataBetweenTime(begin, end);
  }

  base::Time Now() { return test_clock_.Now(); }

  base::Time TimeNotSet() const { return base::Time(); }

  void SetNow(base::Time now) { test_clock_.SetNow(now); }

  std::vector<media::mojom::MediaEngagementScoreDetailsPtr> GetAllScoreDetails()
      const {
    return service_->GetAllScoreDetails();
  }

  bool HasHighEngagement(const url::Origin& origin) const {
    return service_->HasHighEngagement(origin);
  }

  void SetSchemaVersion(int version) { service_->SetSchemaVersion(version); }

  std::vector<MediaEngagementScore> GetAllStoredScores(
      const MediaEngagementService* service) const {
    return service->GetAllStoredScores();
  }

  std::vector<MediaEngagementScore> GetAllStoredScores() const {
    return GetAllStoredScores(service_.get());
  }

 protected:
  scoped_refptr<base::TestMockTimeTaskRunner> mock_time_task_runner_;

 private:
  base::SimpleTestClock test_clock_;

  std::unique_ptr<MediaEngagementService> service_;

  base::ScopedTempDir temp_dir_;

  base::test::ScopedFeatureList scoped_feature_list_;
};

TEST_F(MediaEngagementServiceTest, MojoSerialization) {
  EXPECT_EQ(0u, GetAllScoreDetails().size());

  RecordVisitAndPlaybackAndAdvanceClock(
      url::Origin::Create(GURL("https://www.google.com")));
  EXPECT_EQ(1u, GetAllScoreDetails().size());
}

TEST_F(MediaEngagementServiceTest, RestrictedToHTTPAndHTTPS) {
  url::Origin origin1 = url::Origin::Create(GURL("ftp://www.google.com/"));
  url::Origin origin2 = url::Origin::Create(GURL("file://blah"));
  url::Origin origin3 = url::Origin::Create(GURL("chrome://"));
  url::Origin origin4 = url::Origin::Create(GURL("about://config"));

  RecordVisitAndPlaybackAndAdvanceClock(origin1);
  ExpectScores(origin1, 0.0, 0, 0, TimeNotSet());

  RecordVisitAndPlaybackAndAdvanceClock(origin2);
  ExpectScores(origin2, 0.0, 0, 0, TimeNotSet());

  RecordVisitAndPlaybackAndAdvanceClock(origin4);
  ExpectScores(origin4, 0.0, 0, 0, TimeNotSet());

  RecordVisitAndPlaybackAndAdvanceClock(origin4);
  ExpectScores(origin4, 0.0, 0, 0, TimeNotSet());
}

TEST_F(MediaEngagementServiceTest,
       HandleRecordVisitAndPlaybackAndAdvanceClockion) {
  url::Origin origin1 = url::Origin::Create(GURL("https://www.google.com"));
  ExpectScores(origin1, 0.0, 0, 0, TimeNotSet());
  RecordVisitAndPlaybackAndAdvanceClock(origin1);
  ExpectScores(origin1, 0.05, 1, 1, Now());

  RecordVisit(origin1);
  ExpectScores(origin1, 0.05, 2, 1, Now());

  RecordPlayback(origin1);
  ExpectScores(origin1, 0.1, 2, 2, Now());
  base::Time origin1_time = Now();

  url::Origin origin2 = url::Origin::Create(GURL("https://www.google.co.uk"));
  RecordVisitAndPlaybackAndAdvanceClock(origin2);
  ExpectScores(origin2, 0.05, 1, 1, Now());
  ExpectScores(origin1, 0.1, 2, 2, origin1_time);
}

TEST_F(MediaEngagementServiceTest, IncognitoEngagementService) {
  url::Origin origin1 = url::Origin::Create(GURL("http://www.google.com/"));
  url::Origin origin2 = url::Origin::Create(GURL("https://www.google.com/"));
  url::Origin origin3 = url::Origin::Create(GURL("https://drive.google.com/"));
  url::Origin origin4 = url::Origin::Create(GURL("https://maps.google.com/"));

  RecordVisitAndPlaybackAndAdvanceClock(origin1);
  base::Time origin1_time = Now();
  RecordVisitAndPlaybackAndAdvanceClock(origin2);

  MediaEngagementService* incognito_service =
      MediaEngagementService::Get(profile()->GetOffTheRecordProfile());
  ExpectScores(incognito_service, origin1, 0.05, 1, 1, origin1_time);
  ExpectScores(incognito_service, origin2, 0.05, 1, 1, Now());
  ExpectScores(incognito_service, origin3, 0.0, 0, 0, TimeNotSet());

  incognito_service->RecordVisit(origin3);
  ExpectScores(incognito_service, origin3, 0.0, 1, 0, TimeNotSet());
  ExpectScores(origin3, 0.0, 0, 0, TimeNotSet());

  incognito_service->RecordVisit(origin2);
  ExpectScores(incognito_service, origin2, 0.05, 2, 1, Now());
  ExpectScores(origin2, 0.05, 1, 1, Now());

  RecordVisitAndPlaybackAndAdvanceClock(origin3);
  ExpectScores(incognito_service, origin3, 0.0, 1, 0, TimeNotSet());
  ExpectScores(origin3, 0.05, 1, 1, Now());

  ExpectScores(incognito_service, origin4, 0.0, 0, 0, TimeNotSet());
  RecordVisitAndPlaybackAndAdvanceClock(origin4);
  ExpectScores(incognito_service, origin4, 0.05, 1, 1, Now());
  ExpectScores(origin4, 0.05, 1, 1, Now());
}

TEST_F(MediaEngagementServiceTest, IncognitoOverrideRegularProfile) {
  const url::Origin kOrigin1 = url::Origin::Create(GURL("https://example.org"));
  const url::Origin kOrigin2 = url::Origin::Create(GURL("https://example.com"));

  SetScores(kOrigin1, MediaEngagementScore::GetScoreMinVisits(), 1);
  SetScores(kOrigin2, 1, 0);

  ExpectScores(kOrigin1, 0.05, MediaEngagementScore::GetScoreMinVisits(), 1,
               TimeNotSet());
  ExpectScores(kOrigin2, 0.0, 1, 0, TimeNotSet());

  MediaEngagementService* incognito_service =
      MediaEngagementService::Get(profile()->GetOffTheRecordProfile());
  ExpectScores(incognito_service, kOrigin1, 0.05,
               MediaEngagementScore::GetScoreMinVisits(), 1, TimeNotSet());
  ExpectScores(incognito_service, kOrigin2, 0.0, 1, 0, TimeNotSet());

  // Scores should be the same in incognito and regular profile.
  {
    std::vector<std::pair<url::Origin, double>> kExpectedResults = {
        {kOrigin2, 0.0},
        {kOrigin1, 0.05},
    };

    const auto& scores = GetAllStoredScores();
    const auto& incognito_scores = GetAllStoredScores(incognito_service);

    EXPECT_EQ(kExpectedResults.size(), scores.size());
    EXPECT_EQ(kExpectedResults.size(), incognito_scores.size());

    for (size_t i = 0; i < scores.size(); ++i) {
      EXPECT_EQ(kExpectedResults[i].first, scores[i].origin());
      EXPECT_EQ(kExpectedResults[i].second, scores[i].actual_score());

      EXPECT_EQ(kExpectedResults[i].first, incognito_scores[i].origin());
      EXPECT_EQ(kExpectedResults[i].second, incognito_scores[i].actual_score());
    }
  }

  incognito_service->RecordVisit(kOrigin1);
  RecordPlaybackForService(incognito_service, kOrigin2);

  // Score shouldn't have changed in regular profile.
  {
    std::vector<std::pair<url::Origin, double>> kExpectedResults = {
        {kOrigin2, 0.0},
        {kOrigin1, 0.05},
    };

    const auto& scores = GetAllStoredScores();
    EXPECT_EQ(kExpectedResults.size(), scores.size());

    for (size_t i = 0; i < scores.size(); ++i) {
      EXPECT_EQ(kExpectedResults[i].first, scores[i].origin());
      EXPECT_EQ(kExpectedResults[i].second, scores[i].actual_score());
    }
  }

  // Incognito scores should have the same number of entries but have new
  // values.
  {
    std::vector<std::pair<url::Origin, double>> kExpectedResults = {
        {kOrigin2, 0.05},
        {kOrigin1, 1.0 / 21.0},
    };

    const auto& scores = GetAllStoredScores(incognito_service);
    EXPECT_EQ(kExpectedResults.size(), scores.size());

    for (size_t i = 0; i < scores.size(); ++i) {
      EXPECT_EQ(kExpectedResults[i].first, scores[i].origin());
      EXPECT_EQ(kExpectedResults[i].second, scores[i].actual_score());
    }
  }
}

TEST_F(MediaEngagementServiceTest, CleanupOriginsOnHistoryDeletion) {
  url::Origin origin1 = url::Origin::Create(GURL("http://www.google.com/"));
  url::Origin origin2 = url::Origin::Create(GURL("https://drive.google.com/"));
  url::Origin origin3 = url::Origin::Create(GURL("http://deleted.com/"));
  url::Origin origin4 = url::Origin::Create(GURL("http://notdeleted.com"));

  GURL url1a = GURL("http://www.google.com/search?q=asdf");
  GURL url1b = GURL("http://www.google.com/maps/search?q=asdf");
  GURL url3a = GURL("http://deleted.com/test");

  // origin1 will have a score that is high enough to not return zero
  // and we will ensure it has the same score. origin2 will have a score
  // that is zero and will remain zero. origin3 will have a score
  // and will be cleared. origin4 will have a normal score.
  SetScores(origin1, MediaEngagementScore::GetScoreMinVisits() + 2, 14);
  SetScores(origin2, 2, 1);
  SetScores(origin3, 2, 1);
  SetScores(origin4, MediaEngagementScore::GetScoreMinVisits(), 10);

  base::Time today = GetReferenceTime();
  base::Time yesterday = GetReferenceTime() - base::TimeDelta::FromDays(1);
  base::Time yesterday_afternoon = GetReferenceTime() -
                                   base::TimeDelta::FromDays(1) +
                                   base::TimeDelta::FromHours(4);
  base::Time yesterday_week = GetReferenceTime() - base::TimeDelta::FromDays(8);
  SetNow(today);

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile(), ServiceAccessType::IMPLICIT_ACCESS);

  history->AddPage(origin1.GetURL(), yesterday_afternoon,
                   history::SOURCE_BROWSED);
  history->AddPage(url1a, yesterday_afternoon, history::SOURCE_BROWSED);
  history->AddPage(url1b, yesterday_week, history::SOURCE_BROWSED);
  history->AddPage(origin2.GetURL(), yesterday_afternoon,
                   history::SOURCE_BROWSED);
  history->AddPage(origin3.GetURL(), yesterday_week, history::SOURCE_BROWSED);
  history->AddPage(url3a, yesterday_afternoon, history::SOURCE_BROWSED);

  // Check that the scores are valid at the beginning.
  ExpectScores(origin1, 7.0 / 11.0,
               MediaEngagementScore::GetScoreMinVisits() + 2, 14, TimeNotSet());
  EXPECT_EQ(14.0 / 22.0, GetActualScore(origin1));
  ExpectScores(origin2, 0.05, 2, 1, TimeNotSet());
  EXPECT_EQ(1 / 20.0, GetActualScore(origin2));
  ExpectScores(origin3, 0.05, 2, 1, TimeNotSet());
  EXPECT_EQ(1 / 20.0, GetActualScore(origin3));
  ExpectScores(origin4, 0.5, MediaEngagementScore::GetScoreMinVisits(), 10,
               TimeNotSet());
  EXPECT_EQ(0.5, GetActualScore(origin4));

  {
    base::HistogramTester histogram_tester;
    MediaEngagementChangeWaiter waiter(profile());

    base::CancelableTaskTracker task_tracker;
    // Expire origin1, url1a, origin2, and url3a's most recent visit.
    history->ExpireHistoryBetween(std::set<GURL>(), yesterday, today,
                                  /*user_initiated*/ true, base::DoNothing(),
                                  &task_tracker);
    waiter.Wait();

    // origin1 should have a score that is not zero and is the same as the old
    // score (sometimes it may not match exactly due to rounding). origin2
    // should have a score that is zero but it's visits and playbacks should
    // have decreased. origin3 should have had a decrease in the number of
    // visits. origin4 should have the old score.
    ExpectScores(origin1, 0.6, MediaEngagementScore::GetScoreMinVisits(), 12,
                 TimeNotSet());
    EXPECT_EQ(12.0 / 20.0, GetActualScore(origin1));
    ExpectScores(origin2, 0.0, 1, 0, TimeNotSet());
    EXPECT_EQ(0, GetActualScore(origin2));
    ExpectScores(origin3, 0.0, 1, 0, TimeNotSet());
    ExpectScores(origin4, 0.5, MediaEngagementScore::GetScoreMinVisits(), 10,
                 TimeNotSet());

    histogram_tester.ExpectTotalCount(
        MediaEngagementService::kHistogramURLsDeletedScoreReductionName, 3);
    histogram_tester.ExpectBucketCount(
        MediaEngagementService::kHistogramURLsDeletedScoreReductionName, 5, 2);
    histogram_tester.ExpectBucketCount(
        MediaEngagementService::kHistogramURLsDeletedScoreReductionName, 4, 1);

    histogram_tester.ExpectTotalCount(
        MediaEngagementService::kHistogramClearName, 1);
    histogram_tester.ExpectBucketCount(
        MediaEngagementService::kHistogramClearName, 3, 1);
  }

  {
    base::HistogramTester histogram_tester;
    MediaEngagementChangeWaiter waiter(profile());

    // Expire url1b.
    std::vector<history::ExpireHistoryArgs> expire_list;
    history::ExpireHistoryArgs args;
    args.urls.insert(url1b);
    args.SetTimeRangeForOneDay(yesterday_week);
    expire_list.push_back(args);

    base::CancelableTaskTracker task_tracker;
    history->ExpireHistory(expire_list, base::DoNothing(), &task_tracker);
    waiter.Wait();

    // origin1's score should have changed but the rest should remain the same.
    ExpectScores(origin1, 0.55, MediaEngagementScore::GetScoreMinVisits() - 1,
                 11, TimeNotSet());
    ExpectScores(origin2, 0.0, 1, 0, TimeNotSet());
    ExpectScores(origin3, 0.0, 1, 0, TimeNotSet());
    ExpectScores(origin4, 0.5, MediaEngagementScore::GetScoreMinVisits(), 10,
                 TimeNotSet());

    histogram_tester.ExpectTotalCount(
        MediaEngagementService::kHistogramURLsDeletedScoreReductionName, 1);
    histogram_tester.ExpectBucketCount(
        MediaEngagementService::kHistogramURLsDeletedScoreReductionName, 5, 1);

    histogram_tester.ExpectTotalCount(
        MediaEngagementService::kHistogramClearName, 1);
    histogram_tester.ExpectBucketCount(
        MediaEngagementService::kHistogramClearName, 3, 1);
  }

  {
    base::HistogramTester histogram_tester;
    MediaEngagementChangeWaiter waiter(profile());

    // Expire origin3.
    std::vector<history::ExpireHistoryArgs> expire_list;
    history::ExpireHistoryArgs args;
    args.urls.insert(origin3.GetURL());
    args.SetTimeRangeForOneDay(yesterday_week);
    expire_list.push_back(args);

    base::CancelableTaskTracker task_tracker;
    history->ExpireHistory(expire_list, base::DoNothing(), &task_tracker);
    waiter.Wait();

    // origin3's score should be removed but the rest should remain the same.
    std::map<url::Origin, double> scores = GetScoreMapForTesting();
    EXPECT_TRUE(scores.find(origin3) == scores.end());
    ExpectScores(origin1, 0.55, MediaEngagementScore::GetScoreMinVisits() - 1,
                 11, TimeNotSet());
    ExpectScores(origin2, 0.0, 1, 0, TimeNotSet());
    ExpectScores(origin3, 0.0, 0, 0, TimeNotSet());
    ExpectScores(origin4, 0.5, MediaEngagementScore::GetScoreMinVisits(), 10,
                 TimeNotSet());

    histogram_tester.ExpectTotalCount(
        MediaEngagementService::kHistogramURLsDeletedScoreReductionName, 1);
    histogram_tester.ExpectBucketCount(
        MediaEngagementService::kHistogramURLsDeletedScoreReductionName, 0, 1);

    histogram_tester.ExpectTotalCount(
        MediaEngagementService::kHistogramClearName, 1);
    histogram_tester.ExpectBucketCount(
        MediaEngagementService::kHistogramClearName, 3, 1);
  }
}

TEST_F(MediaEngagementServiceTest, CleanUpDatabaseWhenHistoryIsExpired) {
  base::HistogramTester histogram_tester;

  // |origin1| will have history that is before the expiry threshold and should
  // not be deleted. |origin2| will have history either side of the threshold
  // and should also not be deleted. |origin3| will have history before the
  // threshold and should be deleted.
  url::Origin origin1 = url::Origin::Create(GURL("https://www.google.com"));
  url::Origin origin2 = url::Origin::Create(GURL("https://drive.google.com"));
  url::Origin origin3 = url::Origin::Create(GURL("http://deleted.com"));

  // Populate test MEI data.
  SetScores(origin1, 20, 20);
  SetScores(origin2, 30, 30);
  SetScores(origin3, 40, 40);

  base::Time today = base::Time::Now();
  base::Time before_threshold = today - kHistoryExpirationThreshold;
  SetNow(today);

  // Populate test history records.
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile(), ServiceAccessType::IMPLICIT_ACCESS);

  history->AddPage(origin1.GetURL(), today, history::SOURCE_BROWSED);
  history->AddPage(origin2.GetURL(), today, history::SOURCE_BROWSED);
  history->AddPage(origin2.GetURL(), before_threshold, history::SOURCE_BROWSED);
  history->AddPage(origin3.GetURL(), before_threshold, history::SOURCE_BROWSED);

  // Expire history older than |threshold|.
  MediaEngagementChangeWaiter waiter(profile());
  RestartHistoryService(mock_time_task_runner_);
  // First, run the task that schedules backend initialization.
  mock_time_task_runner_->RunUntilIdle();
  // Now, fast forward time to ensure that the expiration job is completed. 30
  // seconds is the value of kExpirationDelaySec.
  mock_time_task_runner_->FastForwardBy(base::TimeDelta::FromSeconds(30));
  waiter.Wait();

  // Check the scores for the test origins.
  ExpectScores(origin1, 1.0, 20, 20, TimeNotSet());
  ExpectScores(origin2, 1.0, 30, 30, TimeNotSet());
  ExpectScores(origin3, 0, 0, 0, TimeNotSet());

  // Check that we recorded the expiry event to a histogram.
  histogram_tester.ExpectTotalCount(MediaEngagementService::kHistogramClearName,
                                    1);
  histogram_tester.ExpectBucketCount(
      MediaEngagementService::kHistogramClearName, 4, 1);
}

TEST_F(MediaEngagementServiceTest, CleanUpDatabaseWhenHistoryIsDeleted) {
  url::Origin origin1 = url::Origin::Create(GURL("http://www.google.com/"));
  url::Origin origin2 = url::Origin::Create(GURL("https://drive.google.com/"));
  url::Origin origin3 = url::Origin::Create(GURL("http://deleted.com/"));
  url::Origin origin4 = url::Origin::Create(GURL("http://notdeleted.com"));

  GURL url1a = GURL("http://www.google.com/search?q=asdf");
  GURL url1b = GURL("http://www.google.com/maps/search?q=asdf");
  GURL url3a = GURL("http://deleted.com/test");

  // origin1 will have a score that is high enough to not return zero
  // and we will ensure it has the same score. origin2 will have a score
  // that is zero and will remain zero. origin3 will have a score
  // and will be cleared. origin4 will have a normal score.
  SetScores(origin1, MediaEngagementScore::GetScoreMinVisits() + 2, 14);
  SetScores(origin2, 2, 1);
  SetScores(origin3, 2, 1);
  SetScores(origin4, MediaEngagementScore::GetScoreMinVisits(), 10);

  base::Time today = GetReferenceTime();
  base::Time yesterday_afternoon = GetReferenceTime() -
                                   base::TimeDelta::FromDays(1) +
                                   base::TimeDelta::FromHours(4);
  base::Time yesterday_week = GetReferenceTime() - base::TimeDelta::FromDays(8);
  SetNow(today);

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile(), ServiceAccessType::IMPLICIT_ACCESS);

  history->AddPage(origin1.GetURL(), yesterday_afternoon,
                   history::SOURCE_BROWSED);
  history->AddPage(url1a, yesterday_afternoon, history::SOURCE_BROWSED);
  history->AddPage(url1b, yesterday_week, history::SOURCE_BROWSED);
  history->AddPage(origin2.GetURL(), yesterday_afternoon,
                   history::SOURCE_BROWSED);
  history->AddPage(origin3.GetURL(), yesterday_week, history::SOURCE_BROWSED);
  history->AddPage(url3a, yesterday_afternoon, history::SOURCE_BROWSED);

  // Check that the scores are valid at the beginning.
  ExpectScores(origin1, 7.0 / 11.0,
               MediaEngagementScore::GetScoreMinVisits() + 2, 14, TimeNotSet());
  EXPECT_EQ(14.0 / 22.0, GetActualScore(origin1));
  ExpectScores(origin2, 0.05, 2, 1, TimeNotSet());
  EXPECT_EQ(1 / 20.0, GetActualScore(origin2));
  ExpectScores(origin3, 0.05, 2, 1, TimeNotSet());
  EXPECT_EQ(1 / 20.0, GetActualScore(origin3));
  ExpectScores(origin4, 0.5, MediaEngagementScore::GetScoreMinVisits(), 10,
               TimeNotSet());
  EXPECT_EQ(0.5, GetActualScore(origin4));

  {
    base::HistogramTester histogram_tester;

    base::RunLoop run_loop;
    base::CancelableTaskTracker task_tracker;
    // Clear all history.
    history->ExpireHistoryBetween(std::set<GURL>(), base::Time(), base::Time(),
                                  /*user_initiated*/ true,
                                  run_loop.QuitClosure(), &task_tracker);
    run_loop.Run();

    // origin1 should have a score that is not zero and is the same as the old
    // score (sometimes it may not match exactly due to rounding). origin2
    // should have a score that is zero but it's visits and playbacks should
    // have decreased. origin3 should have had a decrease in the number of
    // visits. origin4 should have the old score.
    ExpectScores(origin1, 0.0, 0, 0, TimeNotSet());
    EXPECT_EQ(0, GetActualScore(origin1));
    ExpectScores(origin2, 0.0, 0, 0, TimeNotSet());
    EXPECT_EQ(0, GetActualScore(origin2));
    ExpectScores(origin3, 0.0, 0, 0, TimeNotSet());
    ExpectScores(origin4, 0.0, 0, 0, TimeNotSet());

    histogram_tester.ExpectTotalCount(
        MediaEngagementService::kHistogramURLsDeletedScoreReductionName, 0);

    histogram_tester.ExpectTotalCount(
        MediaEngagementService::kHistogramClearName, 1);
    histogram_tester.ExpectBucketCount(
        MediaEngagementService::kHistogramClearName, 2, 1);
  }
}

TEST_F(MediaEngagementServiceTest, HistoryExpirationIsNoOp) {
  url::Origin origin1 = url::Origin::Create(GURL("http://www.google.com/"));
  url::Origin origin2 = url::Origin::Create(GURL("https://drive.google.com/"));
  url::Origin origin3 = url::Origin::Create(GURL("http://deleted.com/"));
  url::Origin origin4 = url::Origin::Create(GURL("http://notdeleted.com"));

  GURL url1a = GURL("http://www.google.com/search?q=asdf");
  GURL url1b = GURL("http://www.google.com/maps/search?q=asdf");
  GURL url3a = GURL("http://deleted.com/test");

  SetScores(origin1, MediaEngagementScore::GetScoreMinVisits() + 2, 14);
  SetScores(origin2, 2, 1);
  SetScores(origin3, 2, 1);
  SetScores(origin4, MediaEngagementScore::GetScoreMinVisits(), 10);

  ExpectScores(origin1, 7.0 / 11.0,
               MediaEngagementScore::GetScoreMinVisits() + 2, 14, TimeNotSet());
  EXPECT_EQ(14.0 / 22.0, GetActualScore(origin1));
  ExpectScores(origin2, 0.05, 2, 1, TimeNotSet());
  EXPECT_EQ(1 / 20.0, GetActualScore(origin2));
  ExpectScores(origin3, 0.05, 2, 1, TimeNotSet());
  EXPECT_EQ(1 / 20.0, GetActualScore(origin3));
  ExpectScores(origin4, 0.5, MediaEngagementScore::GetScoreMinVisits(), 10,
               TimeNotSet());
  EXPECT_EQ(0.5, GetActualScore(origin4));

  {
    base::HistogramTester histogram_tester;

    history::HistoryService* history = HistoryServiceFactory::GetForProfile(
        profile(), ServiceAccessType::IMPLICIT_ACCESS);

    service()->OnURLsDeleted(
        history, history::DeletionInfo(history::DeletionTimeRange::Invalid(),
                                       true, history::URLRows(),
                                       std::set<GURL>(), base::nullopt));

    // Same as above, nothing should have changed.
    ExpectScores(origin1, 7.0 / 11.0,
                 MediaEngagementScore::GetScoreMinVisits() + 2, 14,
                 TimeNotSet());
    EXPECT_EQ(14.0 / 22.0, GetActualScore(origin1));
    ExpectScores(origin2, 0.05, 2, 1, TimeNotSet());
    EXPECT_EQ(1 / 20.0, GetActualScore(origin2));
    ExpectScores(origin3, 0.05, 2, 1, TimeNotSet());
    EXPECT_EQ(1 / 20.0, GetActualScore(origin3));
    ExpectScores(origin4, 0.5, MediaEngagementScore::GetScoreMinVisits(), 10,
                 TimeNotSet());
    EXPECT_EQ(0.5, GetActualScore(origin4));

    histogram_tester.ExpectTotalCount(
        MediaEngagementService::kHistogramURLsDeletedScoreReductionName, 0);
    histogram_tester.ExpectTotalCount(
        MediaEngagementService::kHistogramClearName, 0);
  }
}

TEST_F(MediaEngagementServiceTest,
       CleanupDataOnSiteDataCleanup_OutsideBoundary) {
  url::Origin origin = url::Origin::Create(GURL("https://www.google.com"));
  base::HistogramTester histogram_tester;

  base::Time today = GetReferenceTime();
  SetNow(today);

  SetScores(origin, 1, 1);
  SetLastMediaPlaybackTime(origin, today);

  ClearDataBetweenTime(today - base::TimeDelta::FromDays(2),
                       today - base::TimeDelta::FromDays(1));
  ExpectScores(origin, 0.05, 1, 1, today);

  histogram_tester.ExpectTotalCount(MediaEngagementService::kHistogramClearName,
                                    1);
  histogram_tester.ExpectBucketCount(
      MediaEngagementService::kHistogramClearName, 1, 1);
}

TEST_F(MediaEngagementServiceTest,
       CleanupDataOnSiteDataCleanup_WithinBoundary) {
  url::Origin origin1 = url::Origin::Create(GURL("https://www.google.com"));
  url::Origin origin2 = url::Origin::Create(GURL("https://www.google.co.uk"));
  base::HistogramTester histogram_tester;

  base::Time today = GetReferenceTime();
  base::Time yesterday = today - base::TimeDelta::FromDays(1);
  base::Time two_days_ago = today - base::TimeDelta::FromDays(2);
  SetNow(today);

  SetScores(origin1, 1, 1);
  SetScores(origin2, 1, 1);
  SetLastMediaPlaybackTime(origin1, yesterday);
  SetLastMediaPlaybackTime(origin2, two_days_ago);

  ClearDataBetweenTime(two_days_ago, yesterday);
  ExpectScores(origin1, 0, 0, 0, TimeNotSet());
  ExpectScores(origin2, 0, 0, 0, TimeNotSet());

  histogram_tester.ExpectTotalCount(MediaEngagementService::kHistogramClearName,
                                    1);
  histogram_tester.ExpectBucketCount(
      MediaEngagementService::kHistogramClearName, 1, 1);
}

TEST_F(MediaEngagementServiceTest, CleanupDataOnSiteDataCleanup_NoTimeSet) {
  url::Origin origin = url::Origin::Create(GURL("https://www.google.com"));
  base::HistogramTester histogram_tester;

  base::Time today = GetReferenceTime();

  SetNow(GetReferenceTime());
  SetScores(origin, 1, 0);

  ClearDataBetweenTime(today - base::TimeDelta::FromDays(2),
                       today - base::TimeDelta::FromDays(1));
  ExpectScores(origin, 0.0, 1, 0, TimeNotSet());

  histogram_tester.ExpectTotalCount(MediaEngagementService::kHistogramClearName,
                                    1);
  histogram_tester.ExpectBucketCount(
      MediaEngagementService::kHistogramClearName, 1, 1);
}

TEST_F(MediaEngagementServiceTest, CleanupDataOnSiteDataCleanup_All) {
  url::Origin origin1 = url::Origin::Create(GURL("https://www.google.com"));
  url::Origin origin2 = url::Origin::Create(GURL("https://www.google.co.uk"));
  base::HistogramTester histogram_tester;

  base::Time today = GetReferenceTime();
  base::Time yesterday = today - base::TimeDelta::FromDays(1);
  base::Time two_days_ago = today - base::TimeDelta::FromDays(2);
  SetNow(today);

  SetScores(origin1, 1, 1);
  SetScores(origin2, 1, 1);
  SetLastMediaPlaybackTime(origin1, yesterday);
  SetLastMediaPlaybackTime(origin2, two_days_ago);

  ClearDataBetweenTime(base::Time(), base::Time::Max());
  ExpectScores(origin1, 0, 0, 0, TimeNotSet());
  ExpectScores(origin2, 0, 0, 0, TimeNotSet());

  histogram_tester.ExpectTotalCount(MediaEngagementService::kHistogramClearName,
                                    1);
  histogram_tester.ExpectBucketCount(
      MediaEngagementService::kHistogramClearName, 0, 1);
}

TEST_F(MediaEngagementServiceTest, HasHighEngagement) {
  url::Origin origin1 = url::Origin::Create(GURL("https://www.google.com"));
  url::Origin origin2 = url::Origin::Create(GURL("https://www.google.co.uk"));
  url::Origin origin3 = url::Origin::Create(GURL("https://www.example.com"));

  SetScores(origin1, 20, 15);
  SetScores(origin2, 20, 4);

  EXPECT_TRUE(HasHighEngagement(origin1));
  EXPECT_FALSE(HasHighEngagement(origin2));
  EXPECT_FALSE(HasHighEngagement(origin3));
}

TEST_F(MediaEngagementServiceTest, SchemaVersion_Changed) {
  url::Origin origin = url::Origin::Create(GURL("https://www.google.com"));
  SetScores(origin, 1, 2);

  SetSchemaVersion(0);
  std::unique_ptr<MediaEngagementService> new_service =
      base::WrapUnique<MediaEngagementService>(
          StartNewMediaEngagementService());

  ExpectScores(new_service.get(), origin, 0.0, 0, 0, TimeNotSet());
  new_service->Shutdown();
}

TEST_F(MediaEngagementServiceTest, SchemaVersion_Same) {
  url::Origin origin = url::Origin::Create(GURL("https://www.google.com"));
  SetScores(origin, 1, 2);

  std::unique_ptr<MediaEngagementService> new_service =
      base::WrapUnique<MediaEngagementService>(
          StartNewMediaEngagementService());

  ExpectScores(new_service.get(), origin, 0.1, 1, 2, TimeNotSet());
  new_service->Shutdown();
}

class MediaEngagementServiceEnabledTest
    : public ChromeRenderViewHostTestHarness {};

TEST_F(MediaEngagementServiceEnabledTest, IsEnabled) {
#if defined(OS_ANDROID)
  // Make sure these flags are disabled on Android
  EXPECT_FALSE(base::FeatureList::IsEnabled(
      media::kMediaEngagementBypassAutoplayPolicies));
  EXPECT_FALSE(
      base::FeatureList::IsEnabled(media::kPreloadMediaEngagementData));
#else
  EXPECT_TRUE(base::FeatureList::IsEnabled(
      media::kMediaEngagementBypassAutoplayPolicies));
  EXPECT_TRUE(base::FeatureList::IsEnabled(media::kPreloadMediaEngagementData));
#endif
}
