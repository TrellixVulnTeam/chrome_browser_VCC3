// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/previews/previews_offline_helper.h"

#include <stdint.h>
#include <string>
#include <vector>

#include "base/feature_list.h"
#include "base/hash/hash.h"
#include "base/optional.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "chrome/browser/offline_pages/offline_page_model_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/offline_pages/buildflags/buildflags.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/previews/core/previews_experiments.h"
#include "components/previews/core/previews_features.h"
#include "content/public/browser/browser_context.h"

namespace {
// Pref key for the available hashed pages kept in class.
const char kHashedAvailablePages[] = "previews.offline_helper.available_pages";

std::string HashURL(const GURL& url) {
  // We are ok with some hash collisions in exchange for non-arbitrary key
  // lengths (as in using the url.spec()). Therefore, use a hash and return that
  // as a string since base::DictionaryValue only accepts strings as keys.
  std::string clean_url = url.GetAsReferrer().spec();
  uint32_t hash = base::PersistentHash(clean_url);
  return base::StringPrintf("%x", hash);
}

std::string TimeToDictionaryValue(base::Time time) {
  return base::NumberToString(time.ToDeltaSinceWindowsEpoch().InMicroseconds());
}

base::Optional<base::Time> TimeFromDictionaryValue(std::string value) {
  int64_t int_value = 0;
  if (!base::StringToInt64(value, &int_value))
    return base::nullopt;

  return base::Time::FromDeltaSinceWindowsEpoch(
      base::TimeDelta::FromMicroseconds(int_value));
}

// Cleans up the given dictionary by removing all stale (expiry has passed)
// entries.
void RemoveStaleOfflinePageEntries(base::DictionaryValue* dict) {
  base::Time earliest_expiry = base::Time::Max();
  std::string earliest_key;
  std::vector<std::string> keys_to_delete;
  for (const auto& iter : dict->DictItems()) {
    // Check for a corrupted value and throw it out if so.
    if (!iter.second.is_string()) {
      keys_to_delete.push_back(iter.first);
      continue;
    }

    base::Optional<base::Time> time =
        TimeFromDictionaryValue(iter.second.GetString());
    if (!time.has_value()) {
      keys_to_delete.push_back(iter.first);
      continue;
    }

    base::Time expiry =
        time.value() + previews::params::OfflinePreviewFreshnessDuration();
    bool is_expired = expiry <= base::Time::Now();

    if (is_expired) {
      keys_to_delete.push_back(iter.first);
      continue;
    }

    if (expiry < earliest_expiry)
      earliest_key = iter.first;
  }

  for (const std::string& key : keys_to_delete)
    dict->RemoveKey(key);

  // RemoveStaleOfflinePageEntries is called for every new added page, so it's
  // fine to just remove one at a time to keep the pref size below a threshold.
  if (dict->DictSize() > previews::params::OfflinePreviewsHelperMaxPrefSize()) {
    dict->RemoveKey(earliest_key);
  }
}

}  // namespace

PreviewsOfflineHelper::PreviewsOfflineHelper(
    content::BrowserContext* browser_context)
    : pref_service_(nullptr),
      available_pages_(std::make_unique<base::DictionaryValue>()),
      offline_page_model_(nullptr) {
  if (!browser_context || browser_context->IsOffTheRecord())
    return;

  pref_service_ = Profile::FromBrowserContext(browser_context)->GetPrefs();

  available_pages_ =
      pref_service_->GetDictionary(kHashedAvailablePages)->CreateDeepCopy();

  // Tidy up the pref in case it's been a while since the last stale item
  // removal.
  RemoveStaleOfflinePageEntries(available_pages_.get());
  UpdatePref();

#if BUILDFLAG(ENABLE_OFFLINE_PAGES)
  offline_page_model_ =
      offline_pages::OfflinePageModelFactory::GetForBrowserContext(
          browser_context);
  if (offline_page_model_)
    offline_page_model_->AddObserver(this);
#endif  // BUILDFLAG(ENABLE_OFFLINE_PAGES)
}

PreviewsOfflineHelper::~PreviewsOfflineHelper() {
  if (offline_page_model_)
    offline_page_model_->RemoveObserver(this);
}

// static
void PreviewsOfflineHelper::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kHashedAvailablePages);
}

bool PreviewsOfflineHelper::ShouldAttemptOfflinePreview(const GURL& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!base::FeatureList::IsEnabled(
          previews::features::kOfflinePreviewsFalsePositivePrevention)) {
    // This is the default behavior without this optimization.
    return true;
  }
  std::string hashed_url = HashURL(url);

  base::Value* value = available_pages_->FindKey(hashed_url);
  if (!value)
    return false;

  if (!value->is_string()) {
    NOTREACHED();
    return false;
  }
  base::Optional<base::Time> time_value =
      TimeFromDictionaryValue(value->GetString());
  if (!time_value.has_value())
    return false;

  base::Time expiry =
      time_value.value() + previews::params::OfflinePreviewFreshnessDuration();
  bool is_expired = expiry <= base::Time::Now();
  if (is_expired) {
    available_pages_->RemoveKey(hashed_url);
    UpdatePref();
  }

  return !is_expired;
}

void PreviewsOfflineHelper::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (offline_page_model_) {
    offline_page_model_->RemoveObserver(this);
    offline_page_model_ = nullptr;
  }
}

void PreviewsOfflineHelper::OfflinePageModelLoaded(
    offline_pages::OfflinePageModel* model) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // TODO(robertogden): Implement.
}

void PreviewsOfflineHelper::OfflinePageAdded(
    offline_pages::OfflinePageModel* model,
    const offline_pages::OfflinePageItem& added_page) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  available_pages_->SetKey(
      HashURL(added_page.url),
      base::Value(TimeToDictionaryValue(added_page.creation_time)));

  // Also remember the original url (pre-redirects) if one exists.
  if (!added_page.original_url_if_different.is_empty()) {
    available_pages_->SetKey(
        HashURL(added_page.original_url_if_different),
        base::Value(TimeToDictionaryValue(added_page.creation_time)));
  }

  RemoveStaleOfflinePageEntries(available_pages_.get());
  UpdatePref();
}

void PreviewsOfflineHelper::OfflinePageDeleted(
    const offline_pages::OfflinePageModel::DeletedPageInfo& page_info) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Has no effect if the url was never in the dictionary.
  available_pages_->RemoveKey(HashURL(page_info.url));

  RemoveStaleOfflinePageEntries(available_pages_.get());
  UpdatePref();
}

void PreviewsOfflineHelper::UpdatePref() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (pref_service_)
    pref_service_->Set(kHashedAvailablePages, *available_pages_);
}
