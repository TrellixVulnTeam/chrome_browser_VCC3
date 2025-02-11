// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/accessibility/ax_validation_message.h"

#include "third_party/blink/renderer/core/html/forms/listed_element.h"
#include "third_party/blink/renderer/core/html/html_element.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/modules/accessibility/ax_object_cache_impl.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"
#include "third_party/skia/include/core/SkMatrix44.h"

namespace blink {

AXValidationMessage::AXValidationMessage(AXObjectCacheImpl& ax_object_cache)
    : AXMockObject(ax_object_cache) {}

AXValidationMessage::~AXValidationMessage() {}

AXObject* AXValidationMessage::ComputeParent() const {
  return AXObjectCache().Root();
}

bool AXValidationMessage::ComputeAccessibilityIsIgnored(
    IgnoredReasons* ignored_reasons) const {
  return false;
}

// TODO(accessibility) Currently we return the bounds of the focused form
// control. If this becomes an issue, return the bounds of the alert itself.
void AXValidationMessage::GetRelativeBounds(AXObject** out_container,
                                            FloatRect& out_bounds_in_container,
                                            SkMatrix44& out_container_transform,
                                            bool* clips_children) const {
  DCHECK(out_container);
  *out_container = nullptr;
  out_bounds_in_container = FloatRect();
  out_container_transform.setIdentity();
  if (clips_children)
    *clips_children = false;

  ListedElement* listed_element = RelatedFormControlIfVisible();
  if (!listed_element)
    return;

  HTMLElement* form_control = ToHTMLElement(listed_element);
  if (!form_control || !form_control->GetLayoutObject())
    return;

  *out_container = ParentObject();

  if (form_control->GetLayoutObject()) {
    out_bounds_in_container =
        FloatRect(form_control->GetLayoutObject()->AbsoluteBoundingBoxRect());
  }
}

bool AXValidationMessage::IsOffScreen() const {
  return false;
}

bool AXValidationMessage::IsVisible() const {
  return RelatedFormControlIfVisible();
}

const AtomicString& AXValidationMessage::LiveRegionStatus() const {
  DEFINE_STATIC_LOCAL(const AtomicString, live_region_status_assertive,
                      ("assertive"));
  return live_region_status_assertive;
}

const AtomicString& AXValidationMessage::LiveRegionRelevant() const {
  DEFINE_STATIC_LOCAL(const AtomicString, live_region_relevant_additions,
                      ("additions"));
  return live_region_relevant_additions;
}

ax::mojom::Role AXValidationMessage::RoleValue() const {
  return ax::mojom::Role::kAlert;
}

ListedElement* AXValidationMessage::RelatedFormControlIfVisible() const {
  AXObject* focused_object = AXObjectCache().FocusedObject();
  if (!focused_object)
    return nullptr;

  Element* element = focused_object->GetElement();
  if (!element)
    return nullptr;

  ListedElement* form_control = ListedElement::From(*element);
  if (!form_control || !form_control->IsValidationMessageVisible())
    return nullptr;

  // The method IsValidationMessageVisible() is a superset of
  // IsNotCandidateOrValid(), but has the benefit of not being true until user
  // has tried to submit data. Waiting until the error message is visible
  // before presenting to screen reader is preferable over hearing about the
  // error while the user is still attempting to input data in the first place.
  return form_control->IsValidationMessageVisible() ? form_control : nullptr;
}

String AXValidationMessage::TextAlternative(
    bool recursive,
    bool in_aria_labelled_by_traversal,
    AXObjectSet& visited,
    ax::mojom::NameFrom& name_from,
    AXRelatedObjectVector* related_objects,
    NameSources* name_sources) const {
  // If nameSources is non-null, relatedObjects is used in filling it in, so it
  // must be non-null as well.
  if (name_sources)
    DCHECK(related_objects);

  ListedElement* form_control_element = RelatedFormControlIfVisible();
  if (!form_control_element)
    return String();

  StringBuilder message;
  message.Append(form_control_element->validationMessage());
  if (form_control_element->ValidationSubMessage()) {
    message.Append(' ');
    message.Append(form_control_element->ValidationSubMessage());
  }

  if (name_sources) {
    name_sources->push_back(NameSource(true));
    name_sources->back().type = ax::mojom::NameFrom::kContents;
    name_sources->back().text = message.ToString();
  }

  return message.ToString();
}

}  // namespace blink
