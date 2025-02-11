// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_NETWORK_PUBLIC_CPP_CORS_PREFLIGHT_CACHE_H_
#define SERVICES_NETWORK_PUBLIC_CPP_CORS_PREFLIGHT_CACHE_H_

#include <map>
#include <memory>
#include <string>

#include "base/component_export.h"
#include "base/macros.h"
#include "base/timer/timer.h"
#include "net/http/http_request_headers.h"
#include "services/network/public/cpp/cors/preflight_result.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"

class GURL;

namespace network {

namespace cors {

// A class to implement CORS-preflight cache that is defined in the fetch spec,
// https://fetch.spec.whatwg.org/#concept-cache.
// TODO(toyoshim): Consider to replace the oldest entry with the new one when
// we have too much cached entries. Also, we want to clear all cached entries
// when users' network configuration is changed.
class COMPONENT_EXPORT(NETWORK_CPP) PreflightCache final {
 public:
  PreflightCache();
  ~PreflightCache();

  // Appends new |preflight_result| entry to the cache for a specified |origin|
  // and |url|.
  void AppendEntry(const std::string& origin,
                   const GURL& url,
                   std::unique_ptr<PreflightResult> preflight_result);

  // Consults with cached results, and decides if we can skip CORS-preflight or
  // not.
  bool CheckIfRequestCanSkipPreflight(
      const std::string& origin,
      const GURL& url,
      mojom::FetchCredentialsMode credentials_mode,
      const std::string& method,
      const net::HttpRequestHeaders& headers,
      bool is_revalidating);

  // Counts cached origins for testing.
  size_t CountOriginsForTesting() const;

  // Counts cached entries for testing.
  size_t CountEntriesForTesting() const;

  // Purges one cache entry if number of entries is larger than |max_entries|
  // for testing.
  void MayPurgeForTesting(size_t max_entries);

 private:
  size_t CountEntries() const;
  void MayPurge(size_t max_entries);
  void ReportMetrics();

  // A map for caching. The outer map takes an origin to find a per-origin
  // cache map, and the inner map takes an URL to find a cached entry.
  std::map<std::string /* origin */,
           std::map<std::string /* url */, std::unique_ptr<PreflightResult>>>
      cache_;

  // RepeatingTimer to report metrics.
  base::RepeatingTimer timer_;

  DISALLOW_COPY_AND_ASSIGN(PreflightCache);
};

}  // namespace cors

}  // namespace network

#endif  // SERVICES_NETWORK_PUBLIC_CPP_CORS_PREFLIGHT_CACHE_H_
