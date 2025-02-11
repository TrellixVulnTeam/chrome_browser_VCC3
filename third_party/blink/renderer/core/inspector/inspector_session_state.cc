// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/inspector/inspector_session_state.h"

#include "third_party/blink/renderer/core/inspector/protocol/Protocol.h"
#include "third_party/inspector_protocol/encoding/encoding.h"

namespace blink {
namespace {
using ::inspector_protocol_encoding::span;
using ::inspector_protocol_encoding::SpanFrom;
using ::inspector_protocol_encoding::cbor::CBORTokenizer;
using ::inspector_protocol_encoding::cbor::CBORTokenTag;
using ::inspector_protocol_encoding::cbor::EncodeDouble;
using ::inspector_protocol_encoding::cbor::EncodeFalse;
using ::inspector_protocol_encoding::cbor::EncodeFromLatin1;
using ::inspector_protocol_encoding::cbor::EncodeFromUTF16;
using ::inspector_protocol_encoding::cbor::EncodeInt32;
using ::inspector_protocol_encoding::cbor::EncodeNull;
using ::inspector_protocol_encoding::cbor::EncodeTrue;
}  // namespace

//
// InspectorSessionState
//
InspectorSessionState::InspectorSessionState(
    mojom::blink::DevToolsSessionStatePtr reattach)
    : reattach_state_(std::move(reattach)),
      updates_(mojom::blink::DevToolsSessionState::New()) {}

const mojom::blink::DevToolsSessionState* InspectorSessionState::ReattachState()
    const {
  return reattach_state_.get();
}

void InspectorSessionState::EnqueueUpdate(const WTF::String& key,
                                          const std::vector<uint8_t>* value) {
  base::Optional<WTF::Vector<uint8_t>> updated_value;
  if (value) {
    WTF::Vector<uint8_t> payload;
    payload.Append(value->data(), value->size());
    updated_value = std::move(payload);
  }
  updates_->entries.Set(key, std::move(updated_value));
}

mojom::blink::DevToolsSessionStatePtr InspectorSessionState::TakeUpdates() {
  auto updates = std::move(updates_);
  updates_ = mojom::blink::DevToolsSessionState::New();
  return updates;
}

//
// Encoding / Decoding routines.
//
/*static*/
void InspectorAgentState::EncodeCBOR(bool v, std::vector<uint8_t>* out) {
  out->push_back(v ? EncodeTrue() : EncodeFalse());
}

/*static*/
bool InspectorAgentState::DecodeCBOR(span<uint8_t> in, bool* v) {
  CBORTokenizer tokenizer(in);
  if (tokenizer.TokenTag() == CBORTokenTag::TRUE_VALUE) {
    *v = true;
    return true;
  }
  if (tokenizer.TokenTag() == CBORTokenTag::FALSE_VALUE) {
    *v = false;
    return true;
  }
  return false;
}

/*static*/
void InspectorAgentState::EncodeCBOR(int32_t v, std::vector<uint8_t>* out) {
  ::inspector_protocol_encoding::cbor::EncodeInt32(v, out);
}

/*static*/
bool InspectorAgentState::DecodeCBOR(span<uint8_t> in, int32_t* v) {
  CBORTokenizer tokenizer(in);
  if (tokenizer.TokenTag() == CBORTokenTag::INT32) {
    *v = tokenizer.GetInt32();
    return true;
  }
  return false;
}

/*static*/
void InspectorAgentState::EncodeCBOR(double v, std::vector<uint8_t>* out) {
  ::inspector_protocol_encoding::cbor::EncodeDouble(v, out);
}

/*static*/
bool InspectorAgentState::DecodeCBOR(span<uint8_t> in, double* v) {
  CBORTokenizer tokenizer(in);
  if (tokenizer.TokenTag() == CBORTokenTag::DOUBLE) {
    *v = tokenizer.GetDouble();
    return true;
  }
  return false;
}

/*static*/
void InspectorAgentState::EncodeCBOR(const WTF::String& v,
                                     std::vector<uint8_t>* out) {
  if (v.Is8Bit()) {
    auto span8 = v.Span8();
    EncodeFromLatin1(span<uint8_t>(span8.data(), span8.size()), out);
  } else {
    auto span16 = v.Span16();
    EncodeFromUTF16(
        span<uint16_t>(reinterpret_cast<const uint16_t*>(span16.data()),
                       span16.size()),
        out);
  }
}

/*static*/
bool InspectorAgentState::DecodeCBOR(span<uint8_t> in, WTF::String* v) {
  CBORTokenizer tokenizer(in);
  if (tokenizer.TokenTag() == CBORTokenTag::STRING8) {
    *v = WTF::String(
        reinterpret_cast<const char*>(tokenizer.GetString8().data()),
        static_cast<size_t>(tokenizer.GetString8().size()));
    return true;
  }
  if (tokenizer.TokenTag() == CBORTokenTag::STRING16) {
    *v = WTF::String(
        reinterpret_cast<const UChar*>(tokenizer.GetString16WireRep().data()),
        tokenizer.GetString16WireRep().size() / 2);
    return true;
  }
  return false;
}

//
// InspectorAgentState
//
InspectorAgentState::InspectorAgentState(const WTF::String& domain_name)
    : domain_name_(domain_name) {}

WTF::String InspectorAgentState::RegisterField(Field* field) {
  WTF::String prefix_key =
      domain_name_ + "." + WTF::String::Number(fields_.size()) + "/";
  fields_.push_back(field);
  return prefix_key;
}

void InspectorAgentState::InitFrom(InspectorSessionState* session_state) {
  for (Field* f : fields_)
    f->InitFrom(session_state);
}

void InspectorAgentState::ClearAllFields() {
  for (Field* f : fields_)
    f->Clear();
}

}  // namespace blink
