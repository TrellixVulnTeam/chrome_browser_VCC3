// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_INSTALLEDAPP_RELATED_APPS_FETCHER_H
#define CONTENT_RENDERER_INSTALLEDAPP_RELATED_APPS_FETCHER_H

#include <string>

#include "base/compiler_specific.h"
#include "content/common/content_export.h"
#include "third_party/blink/public/platform/modules/installedapp/web_related_apps_fetcher.h"

namespace blink {
struct Manifest;
}  // namespace blink

namespace content {

class RenderFrame;

class CONTENT_EXPORT RelatedAppsFetcher : public blink::WebRelatedAppsFetcher {
 public:
  explicit RelatedAppsFetcher(RenderFrame* render_frame);
  ~RelatedAppsFetcher() override;

  // blink::WebRelatedAppsFetcher overrides:
  void GetManifestRelatedApplications(
      blink::GetManifestRelatedApplicationsCallback completion_callback)
      override;

 private:
  // Callback for when the manifest has been fetched, triggered by a call to
  // getManifestRelatedApplications.
  void OnGetManifestForRelatedApplications(
      blink::GetManifestRelatedApplicationsCallback completion_callback,
      const blink::WebURL& url,
      const blink::Manifest& manifest);

  RenderFrame* const render_frame_;

  DISALLOW_COPY_AND_ASSIGN(RelatedAppsFetcher);
};

}  // namespace content

#endif  // CONTENT_RENDERER_INSTALLEDAPP_RELATED_APPS_FETCHER_H
