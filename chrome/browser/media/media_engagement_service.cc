// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/media_engagement_service.h"

#include <functional>

#include "base/bind.h"
#include "base/metrics/histogram_macros.h"
#include "base/time/clock.h"
#include "base/time/default_clock.h"
#include "base/time/time.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/media/media_engagement_contents_observer.h"
#include "chrome/browser/media/media_engagement_score.h"
#include "chrome/browser/media/media_engagement_service_factory.h"
#include "chrome/browser/prerender/prerender_contents.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/history/core/browser/history_service.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "media/base/media_switches.h"
#include "url/origin.h"

const char MediaEngagementService::kHistogramURLsDeletedScoreReductionName[] =
    "Media.Engagement.URLsDeletedScoreReduction";

const char MediaEngagementService::kHistogramClearName[] =
    "Media.Engagement.Clear";

namespace {

// The current schema version of the MEI data. If this value is higher
// than the stored value, all MEI data will be wiped.
static const int kSchemaVersion = 4;

// Do not change the values of this enum as it is used for UMA.
enum class MediaEngagementClearReason {
  kDataAll = 0,
  kDataRange = 1,
  kHistoryAll = 2,
  kHistoryRange = 3,
  kHistoryExpired = 4,
  kCount
};

bool MediaEngagementFilterAdapter(
    const url::Origin& predicate,
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern) {
  url::Origin origin = url::Origin::Create(GURL(primary_pattern.ToString()));
  DCHECK(!origin.opaque());
  return predicate == origin;
}

bool MediaEngagementTimeFilterAdapter(
    MediaEngagementService* service,
    base::Time delete_begin,
    base::Time delete_end,
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern) {
  url::Origin origin = url::Origin::Create(GURL(primary_pattern.ToString()));
  DCHECK(!origin.opaque());
  MediaEngagementScore score = service->CreateEngagementScore(origin);
  base::Time playback_time = score.last_media_playback_time();
  return playback_time >= delete_begin && playback_time <= delete_end;
}

void RecordURLsDeletedScoreReduction(double previous_score,
                                     double current_score) {
  int difference = round((previous_score * 100) - (current_score * 100));
  DCHECK_GE(difference, 0);
  UMA_HISTOGRAM_PERCENTAGE(
      MediaEngagementService::kHistogramURLsDeletedScoreReductionName,
      difference);
}

void RecordClear(MediaEngagementClearReason reason) {
  UMA_HISTOGRAM_ENUMERATION(MediaEngagementService::kHistogramClearName, reason,
                            MediaEngagementClearReason::kCount);
}

}  // namespace

// static
bool MediaEngagementService::IsEnabled() {
  return base::FeatureList::IsEnabled(media::kRecordMediaEngagementScores);
}

// static
MediaEngagementService* MediaEngagementService::Get(Profile* profile) {
  DCHECK(IsEnabled());
  return MediaEngagementServiceFactory::GetForProfile(profile);
}

// static
void MediaEngagementService::CreateWebContentsObserver(
    content::WebContents* web_contents) {
  DCHECK(IsEnabled());

  // Ignore WebContents that are used for prerender/prefetch.
  if (prerender::PrerenderContents::FromWebContents(web_contents))
    return;

  MediaEngagementService* service =
      Get(Profile::FromBrowserContext(web_contents->GetBrowserContext()));
  if (!service)
    return;
  service->contents_observers_.insert(
      {web_contents,
       new MediaEngagementContentsObserver(web_contents, service)});
}

// static
void MediaEngagementService::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterIntegerPref(prefs::kMediaEngagementSchemaVersion, 0, 0);
}

MediaEngagementService::MediaEngagementService(Profile* profile)
    : MediaEngagementService(profile, base::DefaultClock::GetInstance()) {}

MediaEngagementService::MediaEngagementService(Profile* profile,
                                               base::Clock* clock)
    : profile_(profile), clock_(clock) {
  DCHECK(IsEnabled());

  // May be null in tests.
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::IMPLICIT_ACCESS);
  if (history)
    history->AddObserver(this);

  // If kSchemaVersion is higher than what we have stored we should wipe
  // all Media Engagement data.
  if (GetSchemaVersion() < kSchemaVersion) {
    HostContentSettingsMapFactory::GetForProfile(profile_)
        ->ClearSettingsForOneType(CONTENT_SETTINGS_TYPE_MEDIA_ENGAGEMENT);
    SetSchemaVersion(kSchemaVersion);
  }
}

MediaEngagementService::~MediaEngagementService() = default;

int MediaEngagementService::GetSchemaVersion() const {
  return profile_->GetPrefs()->GetInteger(prefs::kMediaEngagementSchemaVersion);
}

void MediaEngagementService::SetSchemaVersion(int version) {
  return profile_->GetPrefs()->SetInteger(prefs::kMediaEngagementSchemaVersion,
                                          version);
}

void MediaEngagementService::ClearDataBetweenTime(
    const base::Time& delete_begin,
    const base::Time& delete_end) {
  if (delete_begin == base::Time() && delete_end == base::Time::Max())
    RecordClear(MediaEngagementClearReason::kDataAll);
  else
    RecordClear(MediaEngagementClearReason::kDataRange);

  HostContentSettingsMapFactory::GetForProfile(profile_)
      ->ClearSettingsForOneTypeWithPredicate(
          CONTENT_SETTINGS_TYPE_MEDIA_ENGAGEMENT, base::Time(),
          base::Time::Max(),
          base::Bind(&MediaEngagementTimeFilterAdapter, this, delete_begin,
                     delete_end));
}

void MediaEngagementService::Shutdown() {
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile_, ServiceAccessType::IMPLICIT_ACCESS);
  if (history)
    history->RemoveObserver(this);
}

void MediaEngagementService::OnURLsDeleted(
    history::HistoryService* history_service,
    const history::DeletionInfo& deletion_info) {
  if (deletion_info.IsAllHistory()) {
    RecordClear(MediaEngagementClearReason::kHistoryAll);

    HostContentSettingsMapFactory::GetForProfile(profile_)
        ->ClearSettingsForOneType(CONTENT_SETTINGS_TYPE_MEDIA_ENGAGEMENT);
    return;
  }

  // If origins are expired by the history service delete them if they have no
  // more visits.
  if (deletion_info.is_from_expiration()) {
    DCHECK(history_service);

    // Build a set of all origins in |deleted_rows|.
    std::set<url::Origin> origins;
    for (const history::URLRow& row : deletion_info.deleted_rows()) {
      origins.insert(url::Origin::Create(row.url()));
    }

    // Check if any origins no longer have any visits.
    RemoveOriginsWithNoVisits(origins, deletion_info.deleted_urls_origin_map());
    return;
  }

  std::map<url::Origin, int> origins;
  for (const history::URLRow& row : deletion_info.deleted_rows()) {
    url::Origin origin = url::Origin::Create(row.url());
    if (origins.find(origin) == origins.end()) {
      origins[origin] = 0;
    }
    origins[origin]++;
  }

  if (!origins.empty())
    RecordClear(MediaEngagementClearReason::kHistoryRange);

  for (auto const& kv : origins) {
    // Remove the number of visits consistent with the number
    // of URLs from the same origin we are removing.
    MediaEngagementScore score = CreateEngagementScore(kv.first);
    double original_score = score.actual_score();
    score.SetVisits(score.visits() - kv.second);

    // If this results in zero visits then clear the score.
    if (score.visits() <= 0) {
      // Score is now set to 0 so the reduction is equal to the original score.
      RecordURLsDeletedScoreReduction(original_score, 0);
      Clear(kv.first);
      continue;
    }

    // Otherwise, recalculate the playbacks to keep the
    // MEI score consistent.
    score.SetMediaPlaybacks(original_score * score.visits());
    score.Commit();

    RecordURLsDeletedScoreReduction(original_score, score.actual_score());
  }
}

void MediaEngagementService::RemoveOriginsWithNoVisits(
    const std::set<url::Origin>& deleted_origins,
    const history::OriginCountAndLastVisitMap& origin_data) {
  // Find all origins that are in |deleted_origins| and not in
  // |remaining_origins| and clear MEI data on them.
  bool has_deleted_origins = false;
  for (const url::Origin& origin : deleted_origins) {
    const auto& origin_count = origin_data.find(origin.GetURL());
    if (origin_count == origin_data.end() || origin_count->second.first > 0)
      continue;

    Clear(origin);
    has_deleted_origins = true;
  }

  if (has_deleted_origins)
    RecordClear(MediaEngagementClearReason::kHistoryExpired);
}

void MediaEngagementService::Clear(const url::Origin& origin) {
  HostContentSettingsMapFactory::GetForProfile(profile_)
      ->ClearSettingsForOneTypeWithPredicate(
          CONTENT_SETTINGS_TYPE_MEDIA_ENGAGEMENT, base::Time(),
          base::Time::Max(),
          base::BindRepeating(&MediaEngagementFilterAdapter,
                              std::cref(origin)));
}

double MediaEngagementService::GetEngagementScore(
    const url::Origin& origin) const {
  return CreateEngagementScore(origin).actual_score();
}

bool MediaEngagementService::HasHighEngagement(
    const url::Origin& origin) const {
  return CreateEngagementScore(origin).high_score();
}

std::map<url::Origin, double> MediaEngagementService::GetScoreMapForTesting()
    const {
  std::map<url::Origin, double> score_map;
  for (MediaEngagementScore& score : GetAllStoredScores())
    score_map[score.origin()] = score.actual_score();
  return score_map;
}

void MediaEngagementService::RecordVisit(const url::Origin& origin) {
  if (!ShouldRecordEngagement(origin))
    return;

  MediaEngagementScore score = CreateEngagementScore(origin);
  score.IncrementVisits();
  score.Commit();
}

std::vector<media::mojom::MediaEngagementScoreDetailsPtr>
MediaEngagementService::GetAllScoreDetails() const {
  std::vector<MediaEngagementScore> data = GetAllStoredScores();

  std::vector<media::mojom::MediaEngagementScoreDetailsPtr> details;
  details.reserve(data.size());
  for (MediaEngagementScore& score : data)
    details.push_back(score.GetScoreDetails());

  return details;
}

MediaEngagementScore MediaEngagementService::CreateEngagementScore(
    const url::Origin& origin) const {
  // If we are in incognito, |settings| will automatically have the data from
  // the original profile migrated in, so all engagement scores in incognito
  // will be initialised to the values from the original profile.
  return MediaEngagementScore(
      clock_, origin, HostContentSettingsMapFactory::GetForProfile(profile_));
}

MediaEngagementContentsObserver* MediaEngagementService::GetContentsObserverFor(
    content::WebContents* web_contents) const {
  const auto& it = contents_observers_.find(web_contents);
  return it == contents_observers_.end() ? nullptr : it->second;
}

Profile* MediaEngagementService::profile() const {
  return profile_;
}

bool MediaEngagementService::ShouldRecordEngagement(
    const url::Origin& origin) const {
  return (origin.scheme() == url::kHttpsScheme ||
          origin.scheme() == url::kHttpScheme);
}

std::vector<MediaEngagementScore> MediaEngagementService::GetAllStoredScores()
    const {
  ContentSettingsForOneType content_settings;
  std::vector<MediaEngagementScore> data;

  HostContentSettingsMap* settings =
      HostContentSettingsMapFactory::GetForProfile(profile_);
  settings->GetSettingsForOneType(CONTENT_SETTINGS_TYPE_MEDIA_ENGAGEMENT,
                                  content_settings::ResourceIdentifier(),
                                  &content_settings);

  // `GetSettingsForOneType` mixes incognito and non-incognito results in
  // incognito profiles creating duplicates. The incognito results are first so
  // we should discard the results following.
  std::map<url::Origin, const ContentSettingPatternSource*> filtered_results;

  for (const auto& site : content_settings) {
    url::Origin origin =
        url::Origin::Create(GURL(site.primary_pattern.ToString()));
    if (origin.opaque()) {
      NOTREACHED();
      continue;
    }

    const auto& result = filtered_results.find(origin);
    if (result != filtered_results.end()) {
      DCHECK(result->second->incognito && !site.incognito);
      continue;
    }

    filtered_results[origin] = &site;
  }

  for (const auto& it : filtered_results) {
    const auto& origin = it.first;
    auto* const site = it.second;

    std::unique_ptr<base::Value> clone =
        base::Value::ToUniquePtrValue(site->setting_value.Clone());

    data.push_back(MediaEngagementScore(
        clock_, origin, base::DictionaryValue::From(std::move(clone)),
        settings));
  }

  return data;
}
