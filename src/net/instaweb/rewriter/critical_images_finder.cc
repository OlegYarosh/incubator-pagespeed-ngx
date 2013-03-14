/*
 * Copyright 2012 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Author: pulkitg@google.com (Pulkit Goyal)

#include "net/instaweb/rewriter/public/critical_images_finder.h"

#include <set>
#include <vector>

#include "base/logging.h"
#include "net/instaweb/rewriter/public/rewrite_driver.h"
#include "net/instaweb/rewriter/public/rewrite_options.h"
#include "net/instaweb/rewriter/public/server_context.h"
#include "net/instaweb/util/public/property_cache.h"
#include "net/instaweb/util/public/scoped_ptr.h"
#include "net/instaweb/util/public/statistics.h"
#include "net/instaweb/util/public/string_util.h"

namespace {
const char kImageUrlSeparator[] = "\n";
}  // namespace

namespace net_instaweb {

const char CriticalImagesFinder::kCriticalImagesPropertyName[] =
    "critical_images";

const char CriticalImagesFinder::kCssCriticalImagesPropertyName[] =
    "css_critical_images";

const char CriticalImagesFinder::kCriticalImagesValidCount[] =
    "critical_images_valid_count";

const char CriticalImagesFinder::kCriticalImagesExpiredCount[] =
    "critical_images_expired_count";

const char CriticalImagesFinder::kCriticalImagesNotFoundCount[] =
    "critical_images_not_found_count";

namespace {
// Append the image url separator to each critical image to enable storing the
// set in the property cache.
void FormatSetForPropertyCache(const StringSet& critical_images,
                               GoogleString* buf) {
  if (!critical_images.empty()) {
    StringSet::iterator it = critical_images.begin();
    StrAppend(buf, *it++);
    for (; it != critical_images.end(); ++it) {
      StrAppend(buf, kImageUrlSeparator, *it);
    }
  }
  if (buf->empty()) {
    // Property cache does not store empty value. So, kImageUrlSeparator is
    // used to denote the empty critical images set.
    *buf = kImageUrlSeparator;
  }
}

}  // namespace

CriticalImagesFinder::CriticalImagesFinder(Statistics* statistics) {
  critical_images_valid_count_ = statistics->GetVariable(
      kCriticalImagesValidCount);
  critical_images_expired_count_ = statistics->GetVariable(
      kCriticalImagesExpiredCount);
  critical_images_not_found_count_ = statistics->GetVariable(
      kCriticalImagesNotFoundCount);
}

CriticalImagesFinder::~CriticalImagesFinder() {
}

void CriticalImagesFinder::InitStats(Statistics* statistics) {
  statistics->AddVariable(kCriticalImagesValidCount);
  statistics->AddVariable(kCriticalImagesExpiredCount);
  statistics->AddVariable(kCriticalImagesNotFoundCount);
}

bool CriticalImagesFinder::IsHtmlCriticalImage(
    const GoogleString& image_url, RewriteDriver* driver) {
  const StringSet* critical_images_set = GetHtmlCriticalImages(driver);
  return ((critical_images_set != NULL) &&
          (critical_images_set->find(image_url) != critical_images_set->end()));
}

bool CriticalImagesFinder::IsCssCriticalImage(
    const GoogleString& image_url, RewriteDriver* driver) {
  const StringSet* critical_images_set = GetCssCriticalImages(driver);
  return ((critical_images_set != NULL) &&
          (critical_images_set->find(image_url) != critical_images_set->end()));
}

const StringSet* CriticalImagesFinder::GetHtmlCriticalImages(
    RewriteDriver* driver) {
  UpdateCriticalImagesSetInDriver(driver);
  const CriticalImagesInfo* info = driver->critical_images_info();
  if (info == NULL) {
    return NULL;
  }

  return info->html_critical_images.get();
}

const StringSet* CriticalImagesFinder::GetCssCriticalImages(
    RewriteDriver* driver) {
  UpdateCriticalImagesSetInDriver(driver);
  const CriticalImagesInfo* info = driver->critical_images_info();
  if (info == NULL) {
    return NULL;
  }

  return info->css_critical_images.get();
}

void CriticalImagesFinder::SetHtmlCriticalImages(
    RewriteDriver* driver, StringSet* critical_images) {
  CriticalImagesInfo* driver_info = driver->critical_images_info();
  // Preserve CSS critical images if they have been updated already.
  if (driver_info == NULL) {
    driver_info = new CriticalImagesInfo;
    driver->set_critical_images_info(driver_info);
  }
  driver_info->html_critical_images.reset(critical_images);
}

void CriticalImagesFinder::SetCssCriticalImages(
    RewriteDriver* driver, StringSet* critical_images) {
  CriticalImagesInfo* driver_info = driver->critical_images_info();
  // Preserve HTML critical images if they have been updated already.
  if (driver_info == NULL) {
    driver_info = new CriticalImagesInfo;
    driver->set_critical_images_info(driver_info);
  }
  driver_info->css_critical_images.reset(critical_images);
}

// Copy the critical images for this request from the property cache into the
// RewriteDriver. The critical images are not stored in CriticalImageFinder
// because the ServerContext holds the CriticalImageFinder and hence is shared
// between requests.
void CriticalImagesFinder::UpdateCriticalImagesSetInDriver(
    RewriteDriver* driver) {
  const CriticalImagesInfo* driver_info = driver->critical_images_info();
  // If driver_info is not NULL, then the CriticalImagesInfo has already been
  // updated, so no need to do anything here.
  if (driver_info != NULL) {
    return;
  }
  scoped_ptr<CriticalImagesInfo> info(new CriticalImagesInfo);
  PropertyCache* page_property_cache =
      driver->server_context()->page_property_cache();
  const PropertyCache::Cohort* cohort =
      page_property_cache->GetCohort(GetCriticalImagesCohort());
  PropertyPage* page = driver->property_page();
  if (page != NULL && cohort != NULL) {
    PropertyValue* property_value = page->GetProperty(
        cohort, kCriticalImagesPropertyName);
    ExtractCriticalImagesSet(driver, property_value, true,
                             info->html_critical_images.get());

    property_value = page->GetProperty(
        cohort, kCssCriticalImagesPropertyName);
    ExtractCriticalImagesSet(driver, property_value, true,
                             info->css_critical_images.get());
  }
  driver->set_critical_images_info(info.release());
}

// TODO(pulkitg): Change all instances of critical_images_set to
// html_critical_images_set.
bool CriticalImagesFinder::UpdateCriticalImagesCacheEntryFromDriver(
    RewriteDriver* driver, StringSet* critical_images_set,
    StringSet* css_critical_images_set) {
  // Update property cache if above the fold critical images are successfully
  // determined.
  PropertyPage* page = driver->property_page();
  PropertyCache* page_property_cache =
      driver->server_context()->page_property_cache();
  return UpdateCriticalImagesCacheEntry(
      page, page_property_cache, critical_images_set, css_critical_images_set);
}

bool CriticalImagesFinder::UpdateCriticalImagesCacheEntry(
    PropertyPage* page, PropertyCache* page_property_cache,
    StringSet* critical_images_set, StringSet* css_critical_images_set) {
  // Update property cache if above the fold critical images are successfully
  // determined.
  scoped_ptr<StringSet> critical_images(critical_images_set);
  scoped_ptr<StringSet> css_critical_images(css_critical_images_set);
  if (page_property_cache != NULL && page != NULL) {
    const PropertyCache::Cohort* cohort =
        page_property_cache->GetCohort(GetCriticalImagesCohort());
    if (cohort != NULL) {
      bool updated = false;
      if (critical_images.get() != NULL) {
        // Update critical images from html.
        GoogleString buf;
        FormatSetForPropertyCache(*critical_images, &buf);
        PropertyValue* property_value = page->GetProperty(
            cohort, kCriticalImagesPropertyName);
        page_property_cache->UpdateValue(buf, property_value);
        updated = true;
      }
      if (css_critical_images.get() != NULL) {
        // Update critical images from css.
        GoogleString buf;
        FormatSetForPropertyCache(*css_critical_images, &buf);
        PropertyValue* property_value = page->GetProperty(
            cohort, kCssCriticalImagesPropertyName);
        page_property_cache->UpdateValue(buf, property_value);
        updated = true;
      }
      return updated;
    } else {
      LOG(WARNING) << "Critical Images Cohort is NULL.";
    }
  }
  return false;
}

// Extract the critical images stored for the given property_value in the
// property page. Returned StringSet will owned by the caller.
void CriticalImagesFinder::ExtractCriticalImagesSet(
    RewriteDriver* driver,
    const PropertyValue* property_value,
    bool track_stats,
    StringSet* critical_images) {
  DCHECK(critical_images != NULL);
  // Don't track stats if we are flushing early, since we will already be
  // counting this when we are rewriting the full page.
  track_stats &= !driver->flushing_early();
  const PropertyCache* page_property_cache =
      driver->server_context()->page_property_cache();
  int64 cache_ttl_ms =
      driver->options()->finder_properties_cache_expiration_time_ms();
  // Check if the cache value exists and is not expired.
  if (property_value->has_value()) {
    const bool is_valid =
        !page_property_cache->IsExpired(property_value, cache_ttl_ms);
    if (is_valid) {
      StringPieceVector critical_images_vector;
      // Get the critical images from the property value. The fourth parameter
      // (true) causes empty strings to be omitted from the resulting
      // vector. kImageUrlSeparator is expected when the critical image set is
      // empty, because the property cache does not store empty values.
      SplitStringPieceToVector(property_value->value(), kImageUrlSeparator,
                               &critical_images_vector, true);
      StringPieceVector::iterator it;
      for (it = critical_images_vector.begin();
           it != critical_images_vector.end();
           ++it) {
        critical_images->insert(it->as_string());
      }
      if (track_stats) {
        critical_images_valid_count_->Add(1);
      }
      return;
    } else if (track_stats) {
      critical_images_expired_count_->Add(1);
    }
  } else if (track_stats) {
    critical_images_not_found_count_->Add(1);
  }
}

}  // namespace net_instaweb
